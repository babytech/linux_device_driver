# memloader test steps

## the `memloader` driver requires a node in device tree
```
firmware-memory {
    compatible = "memloader", "simple-bus";
    load-address = <0x0 0x6e000000>;
    firmware-load;
    firmware-filename = "demo_firmware_filename";
    firmware-blocksize = <0x20000>;
};
```

## copy to `memloader.ko` to target board for example the `/share` dir
## copy the scripts to target board `/share`
```bash
tc_memloader
memloader_userspace_handler
```
## on target board, insert the `memloader.ko`
```bash
# insmod memloader.ko 
memloader_probe: has been run OK
```
## run `tc_memloader` to start test
```bash
# cd /share
# ./tc_memloader
memloader] create test firmware file
4+0 records in
4+0 records out
[tc_memloader] set 'memloader_userspace_handler' as the hotplug handler
[tc_memloader] trigger firmware loading
memloader firmware-memory: Direct firmware load for demo_firmware_filename failed with error -2
memloader firmware-memory: Falling back to user helper
[tc_memloader] verify
[tc_memloader] firmware load-address
0x6e000000
[tc_memloader] firmware load-size
0x400000
[tc_memloader] Load sucess
```
## the log of `memloader_userspace_handler`
```bash
# cat memloader.log
[memloader_userspace_handler] Hello, I am memloader_userspace_handler
[memloader_userspace_handler] fw source: /share/demo_firmware_file
[memloader_userspace_handler] fw target: /sys/class/firmware/demo_firmware_filename/data
[memloader_userspace_handler] Copy start...
[memloader_userspace_handler] Copy done.
```
