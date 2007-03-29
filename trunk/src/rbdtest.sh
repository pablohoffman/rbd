#!/bin/sh
# Remote Block Device - TSO 2006 - Pablo Hoffman, Javier Regusci
#
# Script de testeo para el modulo rbd.
# Por mas informacion, consultar la documentacion.
#
# antes de correr este script levantar dos storage daemon (creando primero 
# los archivos correspondientes)
#
# ./sdfile -s 2 prueba1.rbd
# ./sdfile -s 2 prueba2.rbd
# ./sd -p 8207 prueba1.rbd
# ./sd -p 8208 prueba2.rbd

sd_host="127.0.0.1"
sd_port1="8207"
sd_port2="8208"
testfile="/usr/bin/bc"

success() {
    echo " OK"
}

fail() {
    echo " ERROR!"
    exit 1
}

testing() {
    echo -n ">>> $1..."
}

echo "rbd module tests"
echo

testing "configfs disponible"
mkdir /config 2>/dev/null
umount /config 2>/dev/null
mount -t configfs none /config 2>/dev/null
mount | grep -q configfs && success || fail

testing "descarga modulo"
lsmod | grep -q rbd && {
    umount /mnt 2>/dev/null
    rmdir /config/rbd/* 2>/dev/null
    sleep 2
    rmmod rbd
}
lsmod | grep -q rbd && fail || success

testing "carga modulo"
insmod rbd.ko debug=1
lsmod | grep -q rbd && success || fail

testing "/proc/devices"
cat /proc/devices | grep -q rbd && success || fail

testing "configuracion via configfs"
mkdir /config/rbd/a
echo -n "$sd_host" > /config/rbd/a/host
echo -n "$sd_port1" > /config/rbd/a/port
echo -n "1" > /config/rbd/a/active
sleep 2
[ -b /dev/rbda ] && success || fail

testing "geometria"
sfdisk -g /dev/rbda 2>&1 | grep -q cylinders && success || fail

testing "lectura/escritura cruda"
dd if=$testfile of=/tmp/rbdtest1 bs=512 count=2 2>/dev/null
dd if=/tmp/rbdtest1 of=/dev/rbda bs=512 count=2 seek=10 2>/dev/null
dd if=/dev/rbda of=/tmp/rbdtest2 bs=512 count=2 skip=10 2>/dev/null
diff /tmp/rbdtest1 /tmp/rbdtest2 >/dev/null && success || fail

testing "fdisk - tabla vacia"
sfdisk -q /dev/rbda >/dev/null 2>/dev/null << EOF
unit: sectors
/dev/hda1 : start=        0, size=        0, Id= 0
/dev/hda2 : start=        0, size=        0, Id= 0
/dev/hda3 : start=        0, size=        0, Id= 0
/dev/hda4 : start=        0, size=        0, Id= 0
EOF
cat /proc/partitions | grep -q rbda1 && fail || success

testing "fdisk - una particion"
sfdisk -q /dev/rbda >/dev/null 2>/dev/null << EOF
unit: sectors
/dev/rbda1 : start=        4, size=     4092, Id=83
/dev/rbda2 : start=        0, size=        0, Id= 0
/dev/rbda3 : start=        0, size=        0, Id= 0
/dev/rbda4 : start=        0, size=        0, Id= 0
EOF
cat /proc/partitions | grep -q rbda1 && success || fail

testing "mkfs"
mkfs.ext2 /dev/rbda1 >/dev/null 2>/dev/null && success || fail

testing "mount"
mount /dev/rbda1 /mnt && success || fail

testing "filesystem - mkdir" 
mkdir /mnt/testrbd && success || fail

testing "filesystem - copiar archivo"
cp $testfile /mnt/testrbd/testfile && success || fail

testing "filesystem - verificar archivo"
# desmonto para invalidar cache
umount /dev/rbda1 && mount /dev/rbda1 /mnt  && diff $testfile /mnt/testrbd/testfile >/dev/null && success || fail

testing "configuracion segundo dispositivo"
mkdir /config/rbd/b
echo -n "$sd_host" > /config/rbd/b/host
echo -n "$sd_port2" > /config/rbd/b/port
echo -n "1" > /config/rbd/b/active
sleep 2
[ -b /dev/rbdb ] && success || fail

testing "fdisk - rbd1"
sfdisk -q /dev/rbdb >/dev/null 2>/dev/null << EOF
unit: sectors
/dev/rbdb1 : start=        4, size=     4092, Id=83
/dev/rbdb2 : start=        0, size=        0, Id= 0
/dev/rbdb3 : start=        0, size=        0, Id= 0
/dev/rbdb4 : start=        0, size=        0, Id= 0
EOF

cat /proc/partitions | grep -q rbdb1 && success || fail
testing "mkfs - rbd1"
mkfs.ext2 /dev/rbdb1 >/dev/null 2>/dev/null && success || fail

testing "mount - rbd1"
umount /mnt && mount /dev/rbdb1 /mnt && success || fail
