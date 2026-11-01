# Sustained qualification parent review — 2026-10-31

Accepted the measured Windows native BGRA/NV12 1080p60 performance gate after
source review and fresh offline re-analysis of all four retained 70-second cases.
Traced cases each contain 4,200/4,200 identities at every required stage, including
GPU completion and accepted presentation. Both untraced cases pass aggregate
counts only. No new Windows captures were made during this review.

Reviewed finite-budget implementation `69d7b07` and renderer `7f58e38`; matching
Linux builds and affected tests passed: FrameLink 4/4 (offer bounds, segment,
finite budget, recovery), renderer 3/3 (schedule, clock contract, NV12 CPU).

Corrected `m5_finite_budget.py`: the old loop tested seven malformed CLI values
but printed eight. Added the missing empty-string case and print the actual list
length. The updated Linux finite matrix passed all nine peer scenarios and eight
CLI inputs. Original Windows capture covers seven CLI inputs; separate unit
parsing coverage is not an eighth CLI run. Historical logs remain preserved.

Detailed review and original evidence:
https://github.com/jsuppe/vkrender/blob/feature/m5-platform/docs/m5/sustained-qualification-20261031/PARENT_REVIEW.md

This is not M5 completion, physical scanout, cold physical wake or acoustic sync
qualification. Android adoption/cross-UID work and final integration gates remain.
