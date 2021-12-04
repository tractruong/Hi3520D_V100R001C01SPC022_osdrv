# Hi3520D_V100R001C01SPC022_osdrv
Feasible checking for compiling on HOST
```
$ lsb_release -a
No LSB modules are available.
Distributor ID:	Ubuntu
Description:	Ubuntu 18.04.6 LTS
Release:	18.04
Codename:	bionic

$ uname -r
5.4.0-90-generic
```


## Setup cross compiler 
```
cd osdrv
cd toolchain/arm-hisiv100nptl-linux
chmod +x cross.install
sudo ./cross.install

cd toolchain/arm-hisiv100-linux
chmod +x cross.install 
sudo ./cross.install 

source /etc/profile

cd ../../
sudo dpkg-reconfigure dash -> NO

```

Before
```
ls -l /bin/sh -> /bin/dash
```
After
```
ls -l /bin/sh -> /bin/bash
```
## Compile all
```
make OSDRV_CROSS=arm-hisiv100nptl-linux CHIP=hi3520d OSDRV_SIZE=full all
```
