# 22_partition_tlas - Partitioned Top-Level Acceleration Structures (PTLAS)
#
# This sample demonstrates the VK_NV_partitioned_acceleration_structure extension:
# instances are grouped into partitions, the structure is bound by device address, and
# per-frame updates are applied as a small delta op list against the previous structure.

setup_rt_tutorial_sample(USE_RT_COMMON)
