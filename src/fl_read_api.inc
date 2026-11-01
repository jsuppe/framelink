// Included by each native backend after its channel and retirement helper.
FL_API flResult flLeaseFrame(flChannel* ch, const flFrame* frame, uint64_t* lease) {
    if(!ch || !ch->isConsumer || !frame || !lease || !(ch->flags & FL_DEFER_RELEASE) ||
       (ch->flags & FL_GPU_SYNC)) return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    if(!ch->frameHeld || frame->slot!=ch->heldSlot || frame->_readyValue!=ch->heldReady ||
       frame->slot>=ch->images.size() || frame->image!=ch->images[frame->slot]) return FL_INVALID;
    auto r=ch->readers.acquire(ch->info.generation,*frame,*lease);
    if(r==FL_OK) { ch->frameHeld=false; releaseLeaseReadyFence(ch); }
    return r;
}
FL_API flResult flRetainReadLease(flChannel* ch,uint64_t lease,uint64_t* retained) {
    if(!ch || !ch->isConsumer || !retained)return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    return ch->readers.retain(lease,*retained);
}
FL_API flResult flReadLeaseFrame(flChannel* ch,uint64_t lease,flFrame* frame,uint64_t* generation) {
    if(!ch || !ch->isConsumer || !frame || !generation)return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    return ch->readers.get(lease,*frame,*generation);
}
FL_API flResult flCompleteReadLease(flChannel* ch,uint64_t lease) {
    if(!ch || !ch->isConsumer)return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    // Completion consumes the token, not the retirement operation. Retry a failed
    // retirement with flCollectReadLeases, never complete the same token twice.
    return ch->readers.complete(lease);
}
FL_API flResult flCollectReadLeases(flChannel* ch) {
    if(!ch || !ch->isConsumer)return FL_INVALID;
    std::lock_guard<std::mutex> lock(ch->pendingMutex);
    auto r=ch->readers.retirement();
    if(!r.value)return ch->readers.empty()?FL_OK:FL_BUSY;
    auto result=retireReadLeases(ch,r.value);
    if(result!=FL_OK)return result;
    return ch->readers.retired(r);
}
