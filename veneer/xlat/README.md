# The translation tables (WP-55)

Work in progress.

The divergence classes DR-0000 names, as generated tables rather than as
knowledge in someone's head: the errno value map, the signal number map,
the flag constant maps, and layout descriptors for the structs that cross
the boundary. Each table is extracted mechanically from el8's vendored
headers (`veneer/include/`, the Linux side) and from the WP-26
`newlib-cygwin` tree at `b11613e47` (the Cygwin side), and committed with
a reproduce test in the WP-51 manner.
