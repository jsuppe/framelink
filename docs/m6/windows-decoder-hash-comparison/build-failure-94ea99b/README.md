# Initial CNG integration build failure

Exact94ea99b Windows build failed before test/media execution: `MAXULONG` was not declared under the decoder translation unit include order. Replaced with `std::numeric_limits<ULONG>::max()` in the test-only helper; no digest/row semantics or gate change. Original status/build/idle and upload/collection manifests retained. Runtime idle08:08:53.615Z. Original manifests refer to full external evidence; compact original-byte manifest covers this subset. Fix runtime validation pending at archival time.
