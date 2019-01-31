# UIO driver test steps

### insert the `uio.ko`
- start linux in QEMU in the first terminal
```bash
$ ./script/qemu_linux_start 
```

- in the second terminal
  - copy `uio.ko` to guest linux
  ```bash
  $ ./script/copy_to_linux project/linux_device_driver/uio/uio.ko 
  #1# check the uio.ko in current directory
  -rw-r--r-- 1 guolinp platform 138892 Jan 31 12:49 project/linux_device_driver/uio/uio.ko
  #2# start copy...
  #3# check the uio.ko in guest /share/
  -rw-------    1 root     root        138892 Jan 31 04:50 /share/uio.ko
  ```
  - ssh login the guest linux
  ```bash
  $ ./script/login_linux 
  ```

- in the first terminal, login guest linux
  - get the `irq` eth0 using, it is `32`
  ```bash
  # cat /proc/interrupts 
             CPU0       
   16:      14982     GIC-0  29 Level     twd
   ...
   32:         95     GIC-0  47 Level     eth0
   ...
  ```
  - insert the driver with the argument `uio_irq`
  ```bash
  # cd /share/
  # insmod uio.ko uio_irq=32
  ldd_uio: driver init
  ldd_uio 7c000000.ldd_uio: device probe
  ldd_uio 7c000000.ldd_uio: mapping physical memory at 0x7c000000 (size 0x1000)
  ldd_uio 7c000000.ldd_uio: use customer assigned irq 32, irq_in_device_tree 22
  ldd_uio 7c000000.ldd_uio: registering interrupt 32 to uio device 'ldd_uio_drv'
  ```

### test `uio.ko`
- in the second terminal, press any key, the target is send a char to guest linux, then interrupts will be triggered
```bash
# a
```

- in the first terminal
  - can see some prints
  ```bash
  # ldd_uio_irq_handler: 32 received
  ldd_uio_irq_handler: 32 received
  ```
  - run `lsuio` to have a look
  ```
  # lsuio -vm
  uio0: name=ldd_uio_drv, version=0.0.1, events=5
          map[0]: addr=0x7C000000, size=4096, mmap test: OK
          Device attributes:
          uevent=DRIVER=ldd_uio
          modalias=of:Nldd_uioT<NULL>Cldd_uio
          driver_override=(null)
  ```

### test with application
- to be added
