# The ctype-table accessors: scoping the last stub the cleanest leaf hits

Work in progress (WP-56). bzip2, the acceptance harness's first pinned leaf,
reads `ready` on every libc symbol but one: `__ctype_b_loc`, a bucket-4 stub.
This note scopes that stub and its three kin before any body is written.
