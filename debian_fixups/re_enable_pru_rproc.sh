#!/bin/bash

function escape_slashes {
    sed 's/\//\\\//g' 
}

function escape_periods {
    sed 's/\./\\\./g' 
}

function escape_res {
	sed -r 's/([\$\.\*\/\[\\^])/\\\1/g'
}

function change_line {
    local OLD_LINE_PATTERN=$1; shift
    local NEW_LINE=$1; shift
    local FILE=$1

    local NEW=$(echo "${NEW_LINE}" | escape_res)
	local OLD=$(echo "${OLD_LINE_PATTERN}" | escape_res)
	local SEDSTR="/${OLD}/s/.*/${NEW}/"
	echo "${SEDSTR}"
    sed -i.bak "${SEDSTR}" ${FILE}
    #mv "${FILE}.bak" /tmp/
}

function fix_pruss_rproc {
	change_line 'am33xx-pruss-rproc' \
				'#include "am33xx-pruss-rproc.dtsi"' \
				$1

}

fix_pruss_rproc /opt/source/dtb-4.4-ti/src/arm/am335x-bonegreen-wireless.dts
fix_pruss_rproc /opt/source/dtb-4.4-ti/src/arm/am335x-bonegreen.dts
fix_pruss_rproc /opt/source/dtb-4.4-ti/src/arm/am335x-boneblack.dts

cd /opt/source/dtb-4.4-ti

make src/arm/am335x-bonegreen-wireless.dtb
make src/arm/am335x-bonegreen.dtb
make src/arm/am335x-boneblack.dtb

cp -v src/arm/*.dtb /boot/dtbs/`uname -r`/





