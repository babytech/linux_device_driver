# logbuffer test steps

## compile `logbuffer` driver
  ```bash
  $ ./script/compile_drv logbuffer
  ```
## start uboot in qemu
  ```bash
  $ ./script/qemu_uboot_start
  ```
## press any key to stop in uboot shell, then start linux
  ```bash
  setenv bootargs 'root=/dev/mmcblk0 rw console=ttyAMA0 init=/linuxrc'
  ext2load mmc 0:0 0x60100000 /boot/uImage
  ext2load mmc 0:0 0x60600000 /boot/vexpress-v2p-ca9.dtb
  bootm 0x60100000 - 0x60600000
  ```

## copy `logbuffer.ko`to guest linux
  ```bash
  $ ./script/copy_to_linux  project/linux_device_driver/logbuffer/logbuffer.ko
  ```

## in guest linux, insert `logbuffer.ko`
  ```bash
  # cd /share
  # insmod logbuffer.ko
  ```

## show the uboot log
  ```bash
  # echo uboot-log,0x1000,0x7c001000 > /sys/module/logbuffer/parameters/buffer_info 
  # cat /proc/logbuffer 
  processing buffer at 7c001000 size 1000
  Value(d48faf84)=c0de4ced magic=c0de4ced
  Logbuffer at 0x56ba0bf0, start 0x0, end 0x1d8
  uboot-log: sh: 128 MiB
  uboot-log: :   MMC: 0
  uboot-log:  Warning - bad CRC, using default environment
  uboot-log:  Warning - bad CRC, using default environment
  uboot-log:     serial
  uboot-log: :   serial
  uboot-log: :   serial
  uboot-log: :   smc911x-0
  uboot-log:  any key to stop autoboot:  0 
  uboot-log: setenv bootargs 'root=/dev/mmcblk0 rw console=ttyAMA0 init=/linuxrc'
  uboot-log: ext2load mmc 0:0 0x60100000 /boot/uImage
  uboot-log: 5520 bytes read in 1349 ms (2.6 MiB/s)
  uboot-log: ext2load mmc 0:0 0x60600000 /boot/vexpress-v2p-ca9.dtb
  uboot-log: 65 bytes read in 139 ms (102.5 KiB/s)
  uboot-log: bootm 0x60100000 - 0x60600000
  ```
