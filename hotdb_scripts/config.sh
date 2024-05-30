# Check if the script is run as root
if [ "$(id -u)" != "0" ]; then
    echo "This script must be run as root. Exiting..."
    exit 1
fi

MOUNTPOINT="/mnt/hotdb_test"

DEVICENAME="nvme2n1"
export DEVICENAME
MOUNTDEVICE="/dev/${DEVICENAME}"

# Check if the mount point directory exists; if not, create it
if [ ! -d "$MOUNTPOINT" ]; then
    echo "Directory $MOUNTPOINT does not exist, creating it..."
    mkdir -p "$MOUNTPOINT"
fi

# Check if the device has been formatted as ext4
FS_TYPE=$(blkid -o value -s TYPE "$MOUNTDEVICE")
if [ "$FS_TYPE" != "ext4" ]; then
    echo "Device $MOUNTDEVICE is not formatted as ext4, formatting now..."
    mkfs.ext4 "$MOUNTDEVICE"
fi

# Check if the device is mounted to the specified mount point
if ! grep -qs "$MOUNTPOINT" /proc/mounts; then
    echo "Device $MOUNTDEVICE is not mounted to $MOUNTPOINT, mounting now..."
    mount "$MOUNTDEVICE" "$MOUNTPOINT"
fi

echo "All setups complete, system is ready!"