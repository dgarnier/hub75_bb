cmd_src/arm/BB-LED-ARRAY-00A0.dtbo = cpp -Wp,-MD,src/arm/.BB-LED-ARRAY-00A0.dtbo.d.pre.tmp -nostdinc -Iinclude -Isrc/arm -Itestcase-data -undef -D__DTS__ -x assembler-with-cpp -o src/arm/.BB-LED-ARRAY-00A0.dtbo.dts.tmp src/arm/BB-LED-ARRAY-00A0.dts ; /usr/bin/dtc-v4.1.x -O dtb -o src/arm/BB-LED-ARRAY-00A0.dtbo -b 0 -@ -i src/arm -d src/arm/.BB-LED-ARRAY-00A0.dtbo.d.dtc.tmp src/arm/.BB-LED-ARRAY-00A0.dtbo.dts.tmp ; cat src/arm/.BB-LED-ARRAY-00A0.dtbo.d.pre.tmp src/arm/.BB-LED-ARRAY-00A0.dtbo.d.dtc.tmp > src/arm/.BB-LED-ARRAY-00A0.dtbo.d
BB-LED-ARRAY-00A0.o: src/arm/BB-LED-ARRAY-00A0.dts \
 include/dt-bindings/board/am335x-bbw-bbb-base.h \
 include/dt-bindings/gpio/gpio.h include/dt-bindings/pinctrl/am33xx.h \
 include/dt-bindings/pinctrl/omap.h
src/arm/BB-LED-ARRAY-00A0.dtbo: src/arm/.BB-LED-ARRAY-00A0.dtbo.dts.tmp
