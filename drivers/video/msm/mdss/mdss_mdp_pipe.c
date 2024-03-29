/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"%s: " fmt, __func__

#include <linux/bitmap.h>
#include <linux/errno.h>
#include <linux/mutex.h>

#include "mdss_mdp.h"

#define SMP_MB_SIZE		(mdss_res->smp_mb_size)
#define SMP_MB_CNT		(mdss_res->smp_mb_cnt)
#define SMP_ENTRIES_PER_MB	(SMP_MB_SIZE / 16)
#define SMP_MB_ENTRY_SIZE	16
#define MAX_BPP 4

static DEFINE_MUTEX(mdss_mdp_sspp_lock);
static DEFINE_MUTEX(mdss_mdp_smp_lock);
static DECLARE_BITMAP(mdss_mdp_smp_mmb_pool, MDSS_MDP_SMP_MMB_BLOCKS);

static int mdss_mdp_pipe_free(struct mdss_mdp_pipe *pipe);

static inline void mdss_mdp_pipe_write(struct mdss_mdp_pipe *pipe,
				       u32 reg, u32 val)
{
	writel_relaxed(val, pipe->base + reg);
}

static inline u32 mdss_mdp_pipe_read(struct mdss_mdp_pipe *pipe, u32 reg)
{
	return readl_relaxed(pipe->base + reg);
}

static u32 mdss_mdp_smp_mmb_reserve(unsigned long *existing,
	unsigned long *reserve, size_t n)
{
	u32 i, mmb;

	/* reserve more blocks if needed, but can't free mmb at this point */
	for (i = bitmap_weight(existing, SMP_MB_CNT); i < n; i++) {
		if (bitmap_full(mdss_mdp_smp_mmb_pool, SMP_MB_CNT))
			break;

		mmb = find_first_zero_bit(mdss_mdp_smp_mmb_pool, SMP_MB_CNT);
		set_bit(mmb, reserve);
		set_bit(mmb, mdss_mdp_smp_mmb_pool);
	}
	return i;
}

static int mdss_mdp_smp_mmb_set(int client_id, unsigned long *smp)
{
	u32 mmb, off, data, s;
	int cnt = 0;

	for_each_set_bit(mmb, smp, SMP_MB_CNT) {
		off = (mmb / 3) * 4;
		s = (mmb % 3) * 8;
		data = MDSS_MDP_REG_READ(MDSS_MDP_REG_SMP_ALLOC_W0 + off);
		data &= ~(0xFF << s);
		data |= client_id << s;
		MDSS_MDP_REG_WRITE(MDSS_MDP_REG_SMP_ALLOC_W0 + off, data);
		MDSS_MDP_REG_WRITE(MDSS_MDP_REG_SMP_ALLOC_R0 + off, data);
		cnt++;
	}
	return cnt;
}

static void mdss_mdp_smp_mmb_amend(unsigned long *smp, unsigned long *extra)
{
	bitmap_or(smp, smp, extra, SMP_MB_CNT);
	bitmap_zero(extra, SMP_MB_CNT);
}

static void mdss_mdp_smp_mmb_free(unsigned long *smp, bool write)
{
	if (!bitmap_empty(smp, SMP_MB_CNT)) {
		if (write)
			mdss_mdp_smp_mmb_set(MDSS_MDP_SMP_CLIENT_UNUSED, smp);
		bitmap_andnot(mdss_mdp_smp_mmb_pool, mdss_mdp_smp_mmb_pool,
			      smp, SMP_MB_CNT);
		bitmap_zero(smp, SMP_MB_CNT);
	}
}

static void mdss_mdp_smp_set_wm_levels(struct mdss_mdp_pipe *pipe, int mb_cnt)
{
	u32 fetch_size, val, wm[3];

	fetch_size = mb_cnt * SMP_MB_SIZE;

	/*
	 * when doing hflip, one line is reserved to be consumed down the
	 * pipeline. This line will always be marked as full even if it doesn't
	 * have any data. In order to generate proper priority levels ignore
	 * this region while setting up watermark levels
	 */
	if (pipe->flags & MDP_FLIP_LR) {
		u8 bpp = pipe->src_fmt->is_yuv ? 1 :
			pipe->src_fmt->bpp;
		fetch_size -= (pipe->src.w * bpp);
	}

	/* 1/4 of SMP pool that is being fetched */
	val = (fetch_size / SMP_MB_ENTRY_SIZE) >> 2;

	wm[0] = val;
	wm[1] = wm[0] + val;
	wm[2] = wm[1] + val;

	pr_debug("pnum=%d fetch_size=%u watermarks %u,%u,%u\n", pipe->num,
			fetch_size, wm[0], wm[1], wm[2]);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_REQPRIO_FIFO_WM_0, wm[0]);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_REQPRIO_FIFO_WM_1, wm[1]);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_REQPRIO_FIFO_WM_2, wm[2]);
}

static void mdss_mdp_smp_free(struct mdss_mdp_pipe *pipe)
{
	int i;

	mutex_lock(&mdss_mdp_smp_lock);
	for (i = 0; i < MAX_PLANES; i++) {
		mdss_mdp_smp_mmb_free(&pipe->smp_reserved[i], false);
		mdss_mdp_smp_mmb_free(&pipe->smp[i], true);
	}
	mutex_unlock(&mdss_mdp_smp_lock);
}

void mdss_mdp_smp_unreserve(struct mdss_mdp_pipe *pipe)
{
	int i;

	mutex_lock(&mdss_mdp_smp_lock);
	for (i = 0; i < MAX_PLANES; i++)
		mdss_mdp_smp_mmb_free(&pipe->smp_reserved[i], false);
	mutex_unlock(&mdss_mdp_smp_lock);
}

int mdss_mdp_smp_reserve(struct mdss_mdp_pipe *pipe)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	u32 num_blks = 0, reserved = 0;
	struct mdss_mdp_plane_sizes ps;
	int i;
	int rc = 0, rot_mode = 0;
	u32 nlines;

	if (pipe->bwc_mode) {
		rc = mdss_mdp_get_rau_strides(pipe->src.w, pipe->src.h,
			pipe->src_fmt, &ps);
		if (rc)
			return rc;
		pr_debug("BWC SMP strides ystride0=%x ystride1=%x\n",
			ps.ystride[0], ps.ystride[1]);
	} else if (mdata->has_decimation && pipe->src_fmt->is_yuv) {
		ps.num_planes = 2;
		ps.ystride[0] = pipe->src.w >> pipe->horz_deci;
		ps.ystride[1] = pipe->src.h >> pipe->vert_deci;
	} else {
		rc = mdss_mdp_get_plane_sizes(pipe->src_fmt->format,
			pipe->src.w, pipe->src.h, &ps, 0);
		if (rc)
			return rc;

		if (pipe->mixer && pipe->mixer->rotator_mode)
			rot_mode = 1;
		else if (ps.num_planes == 1)
			ps.ystride[0] = MAX_BPP *
				max(pipe->mixer->width, pipe->src.w);
	}

	nlines = pipe->bwc_mode ? 1 : 2;

	mutex_lock(&mdss_mdp_smp_lock);
	for (i = 0; i < ps.num_planes; i++) {
		if (rot_mode) {
			num_blks = 1;
		} else {
			num_blks = DIV_ROUND_UP(ps.ystride[i] * nlines,
					SMP_MB_SIZE);

			if (mdata->mdp_rev == MDSS_MDP_HW_REV_100)
				num_blks = roundup_pow_of_two(num_blks);
		}

		pr_debug("reserving %d mmb for pnum=%d plane=%d\n",
				num_blks, pipe->num, i);
		reserved = mdss_mdp_smp_mmb_reserve(&pipe->smp[i],
			&pipe->smp_reserved[i], num_blks);

		if (reserved < num_blks)
			break;
	}

	if (reserved < num_blks) {
		pr_debug("insufficient MMB blocks\n");
		for (; i >= 0; i--)
			mdss_mdp_smp_mmb_free(&pipe->smp_reserved[i], false);
		rc = -ENOMEM;
	}
	mutex_unlock(&mdss_mdp_smp_lock);

	return rc;
}

static int mdss_mdp_smp_alloc(struct mdss_mdp_pipe *pipe)
{
	int i;
	int cnt = 0;

	mutex_lock(&mdss_mdp_smp_lock);
	for (i = 0; i < MAX_PLANES; i++) {
		mdss_mdp_smp_mmb_amend(&pipe->smp[i], &pipe->smp_reserved[i]);
		cnt += mdss_mdp_smp_mmb_set(pipe->ftch_id + i, &pipe->smp[i]);
	}
	mdss_mdp_smp_set_wm_levels(pipe, cnt);
	mutex_unlock(&mdss_mdp_smp_lock);
	return 0;
}

void mdss_mdp_smp_release(struct mdss_mdp_pipe *pipe)
{
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON, false);
	mdss_mdp_smp_free(pipe);
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF, false);
}

int mdss_mdp_smp_setup(struct mdss_data_type *mdata, u32 cnt, u32 size)
{
	if (!mdata)
		return -EINVAL;

	mdata->smp_mb_cnt = cnt;
	mdata->smp_mb_size = size;

	return 0;
}

void mdss_mdp_pipe_unmap(struct mdss_mdp_pipe *pipe)
{
	int tmp;

	tmp = atomic_dec_return(&pipe->ref_cnt);

	WARN(tmp < 0, "Invalid unmap with ref_cnt=%d", tmp);
	if (tmp == 0)
		mdss_mdp_pipe_free(pipe);
}

int mdss_mdp_pipe_map(struct mdss_mdp_pipe *pipe)
{
	if (!atomic_inc_not_zero(&pipe->ref_cnt)) {
		pr_err("attempting to map unallocated pipe (%d)", pipe->num);
		return -EINVAL;
	}
	return 0;
}

static struct mdss_mdp_pipe *mdss_mdp_pipe_init(struct mdss_mdp_mixer *mixer,
						u32 type)
{
	struct mdss_mdp_pipe *pipe;
	struct mdss_data_type *mdata;
	struct mdss_mdp_pipe *pipe_pool = NULL;
	u32 npipes;
	u32 i;

	if (!mixer || !mixer->ctl || !mixer->ctl->mdata)
		return NULL;

	mdata = mixer->ctl->mdata;

	switch (type) {
	case MDSS_MDP_PIPE_TYPE_VIG:
		pipe_pool = mdata->vig_pipes;
		npipes = mdata->nvig_pipes;
		break;

	case MDSS_MDP_PIPE_TYPE_RGB:
		pipe_pool = mdata->rgb_pipes;
		npipes = mdata->nrgb_pipes;
		break;

	case MDSS_MDP_PIPE_TYPE_DMA:
		pipe_pool = mdata->dma_pipes;
		npipes = mdata->ndma_pipes;
		break;

	default:
		npipes = 0;
		pr_err("invalid pipe type %d\n", type);
		break;
	}

	for (i = 0; i < npipes; i++) {
		pipe = pipe_pool + i;
		if (atomic_cmpxchg(&pipe->ref_cnt, 0, 1) == 0) {
			pipe->mixer = mixer;
			break;
		}
		pipe = NULL;
	}

	if (pipe) {
		pr_debug("type=%x   pnum=%d\n", pipe->type, pipe->num);
		mutex_init(&pipe->pp_res.hist.hist_mutex);
		spin_lock_init(&pipe->pp_res.hist.hist_lock);
	} else {
		pr_err("no %d type pipes available\n", type);
	}

	return pipe;
}

struct mdss_mdp_pipe *mdss_mdp_pipe_alloc_dma(struct mdss_mdp_mixer *mixer)
{
	struct mdss_mdp_pipe *pipe = NULL;
	struct mdss_data_type *mdata;
	u32 pnum;

	mutex_lock(&mdss_mdp_sspp_lock);
	mdata = mixer->ctl->mdata;
	pnum = mixer->num;

	if (atomic_cmpxchg(&((mdata->dma_pipes[pnum]).ref_cnt), 0, 1) == 0) {
		pipe = &mdata->dma_pipes[pnum];
		pipe->mixer = mixer;

	} else {
		pr_err("DMA pnum%d\t not available\n", pnum);
	}

	mutex_unlock(&mdss_mdp_sspp_lock);
	return pipe;
}

struct mdss_mdp_pipe *mdss_mdp_pipe_alloc(struct mdss_mdp_mixer *mixer,
						 u32 type)
{
	struct mdss_mdp_pipe *pipe;
	mutex_lock(&mdss_mdp_sspp_lock);
	pipe = mdss_mdp_pipe_init(mixer, type);
	mutex_unlock(&mdss_mdp_sspp_lock);
	return pipe;
}

struct mdss_mdp_pipe *mdss_mdp_pipe_get(struct mdss_data_type *mdata, u32 ndx)
{
	struct mdss_mdp_pipe *pipe = NULL;

	if (!ndx)
		return ERR_PTR(-EINVAL);

	mutex_lock(&mdss_mdp_sspp_lock);

	pipe = mdss_mdp_pipe_search(mdata, ndx);
	if (!pipe) {
		pipe = ERR_PTR(-EINVAL);
		goto error;
	}

	if (mdss_mdp_pipe_map(pipe))
		pipe = ERR_PTR(-EACCES);

error:
	mutex_unlock(&mdss_mdp_sspp_lock);
	return pipe;
}

struct mdss_mdp_pipe *mdss_mdp_pipe_search(struct mdss_data_type *mdata,
						  u32 ndx)
{
	u32 i;
	for (i = 0; i < mdata->nvig_pipes; i++) {
		if (mdata->vig_pipes[i].ndx == ndx)
			return &mdata->vig_pipes[i];
	}

	for (i = 0; i < mdata->nrgb_pipes; i++) {
		if (mdata->rgb_pipes[i].ndx == ndx)
			return &mdata->rgb_pipes[i];
	}

	for (i = 0; i < mdata->ndma_pipes; i++) {
		if (mdata->dma_pipes[i].ndx == ndx)
			return &mdata->dma_pipes[i];
	}

	return NULL;
}

static int mdss_mdp_pipe_free(struct mdss_mdp_pipe *pipe)
{
	pr_debug("ndx=%x pnum=%d ref_cnt=%d\n", pipe->ndx, pipe->num,
			atomic_read(&pipe->ref_cnt));

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON, false);
	mdss_mdp_pipe_sspp_term(pipe);
	mdss_mdp_smp_free(pipe);
	pipe->flags = 0;
	pipe->bwc_mode = 0;
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF, false);

	return 0;
}

int mdss_mdp_pipe_destroy(struct mdss_mdp_pipe *pipe)
{
	int tmp;

	tmp = atomic_dec_return(&pipe->ref_cnt);

	if (tmp != 0) {
		pr_err("unable to free pipe %d while still in use (%d)\n",
				pipe->num, tmp);
		return -EBUSY;
	}
	mdss_mdp_pipe_free(pipe);

	return 0;

}

static int mdss_mdp_image_setup(struct mdss_mdp_pipe *pipe)
{
	u32 img_size, src_size, src_xy, dst_size, dst_xy, ystride0, ystride1;
	u32 width, height;
	u32 decimation;

	pr_debug("pnum=%d wh=%dx%d src={%d,%d,%d,%d} dst={%d,%d,%d,%d}\n",
		   pipe->num, pipe->img_width, pipe->img_height,
		   pipe->src.x, pipe->src.y, pipe->src.w, pipe->src.h,
		   pipe->dst.x, pipe->dst.y, pipe->dst.w, pipe->dst.h);

	width = pipe->img_width;
	height = pipe->img_height;
	mdss_mdp_get_plane_sizes(pipe->src_fmt->format, width, height,
			&pipe->src_planes, pipe->bwc_mode);

	if ((pipe->flags & MDP_DEINTERLACE) &&
			!(pipe->flags & MDP_SOURCE_ROTATED_90)) {
		int i;
		for (i = 0; i < pipe->src_planes.num_planes; i++)
			pipe->src_planes.ystride[i] *= 2;
		width *= 2;
		height /= 2;
	}

	decimation = ((1 << pipe->horz_deci) - 1) << 8;
	decimation |= ((1 << pipe->vert_deci) - 1);
	if (decimation)
		pr_debug("Image decimation h=%d v=%d\n",
				pipe->horz_deci, pipe->vert_deci);

	img_size = (height << 16) | width;
	src_size = (pipe->src.h << 16) | pipe->src.w;
	src_xy = (pipe->src.y << 16) | pipe->src.x;
	dst_size = (pipe->dst.h << 16) | pipe->dst.w;
	dst_xy = (pipe->dst.y << 16) | pipe->dst.x;
	ystride0 =  (pipe->src_planes.ystride[0]) |
		    (pipe->src_planes.ystride[1] << 16);
	ystride1 =  (pipe->src_planes.ystride[2]) |
		    (pipe->src_planes.ystride[3] << 16);

	if (pipe->overfetch_disable) {
		img_size = src_size;
		src_xy = 0;
	}

	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_IMG_SIZE, img_size);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_SIZE, src_size);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_XY, src_xy);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_OUT_SIZE, dst_size);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_OUT_XY, dst_xy);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_YSTRIDE0, ystride0);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_YSTRIDE1, ystride1);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_DECIMATION_CONFIG,
		decimation);

	return 0;
}

static int mdss_mdp_format_setup(struct mdss_mdp_pipe *pipe)
{
	struct mdss_mdp_format_params *fmt;
	u32 chroma_samp, unpack, src_format;
	u32 secure = 0;
	u32 opmode;

	fmt = pipe->src_fmt;

	if (pipe->flags & MDP_SECURE_OVERLAY_SESSION)
		secure = 0xF;

	opmode = pipe->bwc_mode;
	if (pipe->flags & MDP_FLIP_LR)
		opmode |= MDSS_MDP_OP_FLIP_LR;
	if (pipe->flags & MDP_FLIP_UD)
		opmode |= MDSS_MDP_OP_FLIP_UD;

	pr_debug("pnum=%d format=%d opmode=%x\n", pipe->num, fmt->format,
			opmode);

	chroma_samp = fmt->chroma_sample;
	if (pipe->flags & MDP_SOURCE_ROTATED_90) {
		if (chroma_samp == MDSS_MDP_CHROMA_H2V1)
			chroma_samp = MDSS_MDP_CHROMA_H1V2;
		else if (chroma_samp == MDSS_MDP_CHROMA_H1V2)
			chroma_samp = MDSS_MDP_CHROMA_H2V1;
	}

	src_format = (chroma_samp << 23) |
		     (fmt->fetch_planes << 19) |
		     (fmt->bits[C3_ALPHA] << 6) |
		     (fmt->bits[C2_R_Cr] << 4) |
		     (fmt->bits[C1_B_Cb] << 2) |
		     (fmt->bits[C0_G_Y] << 0);

	if (pipe->flags & MDP_ROT_90)
		src_format |= BIT(11); /* ROT90 */

	if (fmt->alpha_enable &&
			fmt->fetch_planes != MDSS_MDP_PLANE_INTERLEAVED)
		src_format |= BIT(8); /* SRCC3_EN */

	unpack = (fmt->element[3] << 24) | (fmt->element[2] << 16) |
			(fmt->element[1] << 8) | (fmt->element[0] << 0);
	src_format |= ((fmt->unpack_count - 1) << 12) |
			(fmt->unpack_tight << 17) |
			(fmt->unpack_align_msb << 18) |
			((fmt->bpp - 1) << 9);

	mdss_mdp_pipe_sspp_setup(pipe, &opmode);

	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_FORMAT, src_format);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_UNPACK_PATTERN, unpack);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_OP_MODE, opmode);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_ADDR_SW_STATUS, secure);

	return 0;
}

int mdss_mdp_pipe_addr_setup(struct mdss_data_type *mdata, u32 *offsets,
				u32 *ftch_id, u32 type, u32 num_base, u32 len)
{
	struct mdss_mdp_pipe *head;
	u32 i;
	int rc = 0;

	head = devm_kzalloc(&mdata->pdev->dev, sizeof(struct mdss_mdp_pipe) *
			len, GFP_KERNEL);

	if (!head) {
		pr_err("unable to setup pipe type=%d :devm_kzalloc fail\n",
			type);
		return -ENOMEM;
	}

	for (i = 0; i < len; i++) {
		head[i].type = type;
		head[i].ftch_id  = ftch_id[i];
		head[i].num = i + num_base;
		head[i].ndx = BIT(i + num_base);
		head[i].base = mdata->mdp_base + offsets[i];
	}

	switch (type) {

	case MDSS_MDP_PIPE_TYPE_VIG:
		mdata->vig_pipes = head;
		break;

	case MDSS_MDP_PIPE_TYPE_RGB:
		mdata->rgb_pipes = head;
		break;

	case MDSS_MDP_PIPE_TYPE_DMA:
		mdata->dma_pipes = head;
		break;

	default:
		pr_err("Invalid pipe type=%d\n", type);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int mdss_mdp_src_addr_setup(struct mdss_mdp_pipe *pipe,
				   struct mdss_mdp_data *data)
{
	struct mdss_data_type *mdata = mdss_mdp_get_mdata();
	int ret = 0;

	pr_debug("pnum=%d\n", pipe->num);

	data->bwc_enabled = pipe->bwc_mode;

	ret = mdss_mdp_data_check(data, &pipe->src_planes);
	if (ret)
		return ret;

	if (pipe->overfetch_disable)
		mdss_mdp_data_calc_offset(data, pipe->src.x, pipe->src.y,
			&pipe->src_planes, pipe->src_fmt);

	/* planar format expects YCbCr, swap chroma planes if YCrCb */
	if (mdata->mdp_rev < MDSS_MDP_HW_REV_102 &&
			(pipe->src_fmt->fetch_planes == MDSS_MDP_PLANE_PLANAR)
				&& (pipe->src_fmt->element[0] == C1_B_Cb))
		swap(data->p[1].addr, data->p[2].addr);

	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC0_ADDR, data->p[0].addr);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC1_ADDR, data->p[1].addr);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC2_ADDR, data->p[2].addr);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC3_ADDR, data->p[3].addr);

	return 0;
}

static int mdss_mdp_pipe_solidfill_setup(struct mdss_mdp_pipe *pipe)
{
	int ret;
	u32 secure, format;

	pr_debug("solid fill setup on pnum=%d\n", pipe->num);

	ret = mdss_mdp_image_setup(pipe);
	if (ret) {
		pr_err("image setup error for pnum=%d\n", pipe->num);
		return ret;
	}

	format = MDSS_MDP_FMT_SOLID_FILL;
	secure = (pipe->flags & MDP_SECURE_OVERLAY_SESSION ? 0xF : 0x0);

	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_FORMAT, format);
	mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_SSPP_SRC_ADDR_SW_STATUS, secure);

	return 0;
}

int mdss_mdp_pipe_queue_data(struct mdss_mdp_pipe *pipe,
			     struct mdss_mdp_data *src_data)
{
	int ret = 0;
	u32 params_changed, opmode;

	if (!pipe) {
		pr_err("pipe not setup properly for queue\n");
		return -ENODEV;
	}

	if (!pipe->mixer) {
		pr_err("pipe mixer not setup properly for queue\n");
		return -ENODEV;
	}

	pr_debug("pnum=%x mixer=%d play_cnt=%u\n", pipe->num,
		 pipe->mixer->num, pipe->play_cnt);

	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_ON, false);

	params_changed = pipe->params_changed;
	if (src_data == NULL) {
		mdss_mdp_pipe_solidfill_setup(pipe);
		goto update_nobuf;
	}

	if (params_changed) {
		pipe->params_changed = 0;

		ret = mdss_mdp_pipe_pp_setup(pipe, &opmode);
		if (ret) {
			pr_err("pipe pp setup error for pnum=%d\n", pipe->num);
			goto done;
		}

		ret = mdss_mdp_image_setup(pipe);
		if (ret) {
			pr_err("image setup error for pnum=%d\n", pipe->num);
			goto done;
		}

		ret = mdss_mdp_format_setup(pipe);
		if (ret) {
			pr_err("format %d setup error pnum=%d\n",
			       pipe->src_fmt->format, pipe->num);
			goto done;
		}

		if (pipe->type == MDSS_MDP_PIPE_TYPE_VIG)
			mdss_mdp_pipe_write(pipe, MDSS_MDP_REG_VIG_OP_MODE,
			opmode);

		mdss_mdp_smp_alloc(pipe);
	}

	ret = mdss_mdp_src_addr_setup(pipe, src_data);
	if (ret) {
		pr_err("addr setup error for pnum=%d\n", pipe->num);
		goto done;
	}

update_nobuf:
	mdss_mdp_mixer_pipe_update(pipe, params_changed);

	pipe->play_cnt++;

done:
	mdss_mdp_clk_ctrl(MDP_BLOCK_POWER_OFF, false);

	return ret;
}

int mdss_mdp_pipe_is_staged(struct mdss_mdp_pipe *pipe)
{
	return (pipe == pipe->mixer->stage_pipe[pipe->mixer_stage]);
}
