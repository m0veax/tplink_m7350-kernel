/*
 * Copyright (c) 2011, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef POST_BUFFER_LOCK_H
#define POST_BUFFER_LOCK_H

#include <stdint.h>
#include <sys/types.h>
#include <utils/threads.h>
#include <utils/Vector.h>

namespace android {

class LayerBuffer;
class LayerBase;

/* Helper class like std::count_if */
template <class T, class F>
unsigned int count_if(T* begin, T* end, F f){
  unsigned int c=0;
  while (begin != end){
    if(f(*begin)) ++c;
    ++begin;
  }
  return c;
}

/* helper class like std::equal */
template <class T>
struct equal{
  equal(unsigned int i) : _i (i) {}
  bool operator()(T a) const {
    return a == _i;
  }
  T _i;
};

/*
 * Policy base for postBuffer.
 * */
class PostBufPolicyBase {
public:
  typedef enum { POSTBUF_GO, POSTBUF_BLOCK } postBufState_t;
  /*
   * Wait function would implement waiting mechanism for blocking postbuffer
   * */
  virtual void wait(Mutex & m, Condition& c, postBufState_t& s) {}

  /*
   * onQueueBuf would implement mechanism for transition to GO state
   * */
   virtual void onQueueBuf(Mutex & m, Condition& c,
                           bool dirtyBit, postBufState_t& s) {}
  virtual ~PostBufPolicyBase(){}
};

/*
 * No op for No blocking/lock policy
 * */
class PostBufNoLockPolicy : public PostBufPolicyBase {} ;

/*
 * Locking policy. Implementing wait with locks
 * */
class PostBufLockPolicy : public PostBufPolicyBase {
public:
  /*
   * Wait function would implement waiting mechanism for blocking postbuffer
   * */
  virtual void wait(Mutex & m, Condition& c, postBufState_t& s);

  /*
   * onQueueBuf would implement mechanism for transition to GO state
   * */
   virtual void onQueueBuf(Mutex & m, Condition& c,
                           bool dirtyBit, postBufState_t& s){
    Mutex::Autolock _l(m);
    if(dirtyBit) { s = PostBufPolicyBase::POSTBUF_GO; c.signal(); }
  }
};


/*
 * Singleton for PostBuffer policy. The main purpose of that class is
 * to host different wait/lock policy for postBuffer and let use cases
 * like videowall to play w/o any locking on postbuffer
 * */
class PostBufferSingleton
{
public:
  /*
   * Instance accessor/creator. Assumption is that SurfaceFlinger will hit that
   * one first (before and IBinder)
   * */
  static PostBufferSingleton* instance() {
    // FIXME protect MT
    if(!mInstance){ mInstance = new PostBufferSingleton; }
    return mInstance;
  }
  /*
   * Setting policy according to num of layers
   * Using n/m (n out of m occurrences)
   */
  void setPolicy(unsigned int numLayers){
    // When numLayers is 0, it means:
    // 1. Some targets have no overlay (but do use LayerBuffer).
    // 2. When number of layer buffers is 0, it means no layer buffers in the
    // system. It is safe in that case to have _any_ policy and just return.
    // Choosing NoLockPolicy in order to make it work for(1) and similar targets
    if(!numLayers) {
      mPolicy = &mNoLockPolicy;
      return;
    }
    setLayer(numLayers);
    setPolicy_i();
  }

  /*
   * Pass through for the wait policy function
   */
  void wait(Mutex & m, Condition& c, PostBufPolicyBase::postBufState_t& s){
    mPolicy->wait(m, c, s);
  }

  /*
   * Pass through for the onQueueBuf policy function
   */
   void onQueueBuf(Mutex & m, Condition& c, bool dirtyBit,
                   PostBufPolicyBase::postBufState_t& s) {
      mPolicy->onQueueBuf(m, c, dirtyBit, s);
  }

  /*
   * Add push buffer layer to a list that track it
   */
   void addPushBufferLayers(const Vector< sp<LayerBase> >& l);

   /*
    * Call onQueueBuf for each cached layer mPushBufList
    * */
   void onQueueBuf();

   /*
    * Clear push buffer list.
    * */
   void clearBufferLayerList() { mPushBufList.clear(); }
private:
  /*
   * Ctor/Copy/Assignment and instance here
   */
  explicit PostBufferSingleton() : mPolicy(&mLockPolicy), mCurrentSlot(0) {
    ::memset(mLastNumLayers, 1, sizeof (mLastNumLayers));
  }
  PostBufferSingleton(const PostBufferSingleton&);
  PostBufferSingleton& operator=(const PostBufferSingleton&);
  static PostBufferSingleton* mInstance;

  /* set policy based on how many 1's layers vs >1 layers */
  void setPolicy_i(){
    /* if we have high count of 1's we will have LockPolicy  */
    if (countSingleLayer()){
      mPolicy = &mLockPolicy;
    }
    else{
      mPolicy = &mNoLockPolicy;
    }
  }

  void setLayer(unsigned int l){
    mLastNumLayers[mCurrentSlot++ % MAX_WIN_ARRAY] = l;
  }

  /* count single layers occurrences  */
  bool countSingleLayer(){
    return (count_if(mLastNumLayers, mLastNumLayers + MAX_WIN_ARRAY,
        equal<int>(1)) >= MAX_WIN_SIZE);
  }

  /* base/lock/no policy */
  PostBufPolicyBase* mPolicy;
  PostBufLockPolicy mLockPolicy;
  PostBufNoLockPolicy mNoLockPolicy;

   /* push buffer layer list. Used in SurfaceFlinger to signal
    * all pending push buffer layers*/
   Vector< sp<LayerBase> > mPushBufList;

  /* "sliding window" enums */
  enum { MAX_WIN_SIZE = 4, MAX_WIN_ARRAY = 6 };

  /* "sliding window" current slot and vector of
   * last layers captured
   */
  unsigned int mCurrentSlot;
  unsigned int mLastNumLayers[MAX_WIN_ARRAY];
};

}

#endif // _BUFFER_LOCK_H
