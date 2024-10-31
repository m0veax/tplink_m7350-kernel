#include <linux/ieee80211.h>
#include <linux/export.h>
#include <linux/timer.h>
#include <net/cfg80211.h>
#include "nl80211.h"
#include "core.h"


static int __cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
			      struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	ASSERT_WDEV_LOCK(wdev);

	if (!rdev->ops->stop_ap)
		return -EOPNOTSUPP;

	if (dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP &&
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_P2P_GO)
		return -EOPNOTSUPP;

	if (!wdev->beacon_interval)
		return -ENOENT;

	err = rdev->ops->stop_ap(&rdev->wiphy, dev);
	if (!err) {
		wdev->beacon_interval = 0;
		wdev->channel = NULL;
	}

	return err;
}

int cfg80211_stop_ap(struct cfg80211_registered_device *rdev,
		     struct net_device *dev)
{
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	int err;

	wdev_lock(wdev);
	err = __cfg80211_stop_ap(rdev, dev);
	wdev_unlock(wdev);

	return err;
}



/*
 * AP Start Failure Notify timer
 */
static struct timer_list _asfn_timer;

/*
 * timer-function_ap-start-failure-notify
 */
static void _tf_asfn(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;

	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct wiphy *wiphy = wdev->wiphy;
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wiphy);

	nl80211_ap_start_failure_notify(rdev, dev, GFP_KERNEL);
}

int cfg80211_ap_start_failure_notify(struct net_device *dev)
{
	/*
	 * Set a timer to send msg of AP start failure
	 * so that upper layer app(QCMAP/Hostapd Agent)
	 * can get notified.
	 */
	init_timer(&_asfn_timer);
	_asfn_timer.expires = jiffies + HZ * 5;
	_asfn_timer.function = _tf_asfn;
	_asfn_timer.data = (unsigned long)dev;

	add_timer(&_asfn_timer);

	return 0;
}
EXPORT_SYMBOL(cfg80211_ap_start_failure_notify);


int cfg80211_ap_fw_error_notify(struct wiphy *wy)
{
	struct cfg80211_registered_device *rdev = wiphy_to_dev(wy);
	nl80211_ap_fw_error_notify(rdev, GFP_KERNEL);

	return 0;
}
EXPORT_SYMBOL(cfg80211_ap_fw_error_notify);
