
# #!/bin/bash

# # Create cgroup for kv
# CGROUP_PATH=/sys/fs/cgroup/memory

# for cg in kv32; do
#     if [ ! -d "$CGROUP_PATH/$cg" ]; then
#         mkdir $CGROUP_PATH/$cg
#     fi
# done

# # Limit the memory and cpu resources
# # echo 4G > $CGROUP_PATH/kv4/memory.limit_in_bytes
# # echo 16G > $CGROUP_PATH/kv16/memory.limit_in_bytes
# echo 32G > $CGROUP_PATH/kv32/memory.limit_in_bytes
# # echo 80G > $CGROUP_PATH/kv80/memory.limit_in_bytes
# # echo 0 > $CGROUP_PATH/kv4/memory.swappiness
# # echo 0 > $CGROUP_PATH/kv16/memory.swappiness
# echo 0 > $CGROUP_PATH/kv32/memory.swappiness
# # echo 0 > $CGROUP_PATH/kv80/memory.swappiness





# # Change the owner to the specific user
# if id "root" &>/dev/null; then
#     chown -R root:root $CGROUP_PATH/kv4 $CGROUP_PATH/kv16 $CGROUP_PATH/kv64 $CGROUP_PATH/kv80
# else
#     echo "User 'root' does not exist. Skipping chown."
# fi

# [ ! -d "/mnt/ssd" ] && mkdir -p /mnt/ssd
# [ ! -d "/mnt/nvm" ] && mkdir -p /mnt/nvm

# # Mount the devices if they exist
# if [ -b /dev/sda ]; then
#     [ ! -d /mnt/ssd ] && mkdir -p /mnt/ssd
#     mount /dev/sdb1 /mnt/ssd # where the store writes log
# else
#     echo "Device /dev/sdb1 does not exist."
# fi

# if [ -b /dev/sdb ]; then
#     [ ! -d /mnt/nvm ] && mkdir -p /mnt/nvm
#     mount /dev/sda2 /mnt/nvm # where the store keeps all the records
# else
#     echo "Device /dev/sda2 does not exist."
# fi

# Set CPU performance
cmd='-g performance'
MAX_CPU=$((`nproc --all` - 1))
for i in $(seq 0 $MAX_CPU); do
    echo "Changing CPU $i with parameter $cmd";
    sudo cpufreq-set -c $i $cmd ;
done

