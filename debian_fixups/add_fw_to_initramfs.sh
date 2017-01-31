#!/bin/bash

if [ ! -f /etc/initramfs-tools/hooks/pruss ] ; then
	
	cat <<EOF > /etc/initramfs-tools/hooks/pruss 	
#!/bin/sh -e
# Copy the am335x-pru?-fw into the initramfs

if [ "\$1" = "prereqs" ]; then exit 0; fi

. /usr/share/initramfs-tools/hook-functions

if [ -d /lib/firmware/ ] ; then
    mkdir -p \$DESTDIR/lib/firmware
    if [ -f /lib/firmware/am335x-pru0-fw ] ; then
		echo "copying pru0-fw"
        cp -a /lib/firmware/am335x-pru0-fw \$DESTDIR/lib/firmware/
    elif [ -f \$DESTDIR/lib/firmware/am335x-pru0-fw ] ; then
        rm \$DESTDIR/lib/firmware/am335x-pru0-fw
    fi
    if [ -f /lib/firmware/am335x-pru1-fw ] ; then
		echo "copying pru1-fw"
        cp -a /lib/firmware/am335x-pru1-fw \$DESTDIR/lib/firmware/
    elif [ -f \$DESTDIR/lib/firmware/am335x-pru1-fw ] ; then
        rm \$DESTDIR/lib/firmware/am335x-pru1-fw
    fi
fi
EOF
chmod a+x  /etc/initramfs-tools/hooks/pruss

fi

update_initramfs () {
	if [ -f /boot/initrd.img-$(uname -r) ] ; then
		sudo update-initramfs -u -k $(uname -r)
	else
		sudo update-initramfs -c -k $(uname -r)
	fi
}

update_initramfs

