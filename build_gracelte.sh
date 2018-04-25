#!/bin/bash
# kernel build script by Tkkg1994 v0.6 (optimized from apq8084 kernel source)

export MODEL=gracelte
export VARIANT=eur
export ARCH=arm64
export VERSION=V1.0.0
export BUILD_CROSS_COMPILE=~/Desktop/kernel/toolchains/aarch64-cortex_a53-linux-gnueabi-gcc-6/bin/aarch64-cortex_a53-linux-gnueabi-
export BUILD_JOB_NUMBER=`grep processor /proc/cpuinfo|wc -l`

RDIR=$(pwd)
OUTDIR=$RDIR/arch/$ARCH/boot
DTSDIR=$RDIR/arch/$ARCH/boot/dts
DTBDIR=$OUTDIR/dtb
DTCTOOL=$RDIR/scripts/dtc/dtc
INCDIR=$RDIR/include
KERNELNAME=KimcilKernel
KERNEL_VERSION=V1.0.0
PAGE_SIZE=2048
DTB_PADDING=0

case $MODEL in
gracelte)
	case $VARIANT in
	can|duos|eur|xx)
		KERNEL_DEFCONFIG=exynos8890-gracelte_defconfig
		;;
	*)
		echo "Unknown variant: $VARIANT"
		exit 1
		;;
	esac
;;
*)
	echo "Unknown device: $MODEL"
	exit 1
	;;
esac

FUNC_CLEAN_DTB()
{
	if ! [ -d $RDIR/arch/$ARCH/boot/dts ] ; then
		echo "no directory : "$RDIR/arch/$ARCH/boot/dts""
	else
		echo "rm files in : "$RDIR/arch/$ARCH/boot/dts/*.dtb""
		rm $RDIR/arch/$ARCH/boot/dts/*.dtb
		rm $RDIR/arch/$ARCH/boot/dtb/*.dtb
		rm $RDIR/arch/$ARCH/boot/boot.img-dtb
		rm $RDIR/arch/$ARCH/boot/boot.img-zImage
	fi
}

FUNC_BUILD_DTIMAGE_TARGET()
{
	[ -f "$DTCTOOL" ] || {
		echo "You need to run ./build.sh first!"
		exit 1
	}

	case $MODEL in
	gracelte)
		case $VARIANT in
		can|duos|eur|xx)
			DTSFILES="exynos8890-gracelte_eur_open_00 exynos8890-gracelte_eur_open_01
					exynos8890-gracelte_eur_open_02 exynos8890-gracelte_eur_open_03
					exynos8890-gracelte_eur_open_05 exynos8890-gracelte_eur_open_07
					exynos8890-gracelte_eur_open_09 exynos8890-gracelte_eur_open_11"
			;;
		esac
	;;
	*)
		echo "Unknown device: $MODEL"
		exit 1
		;;
	esac

	mkdir -p $OUTDIR $DTBDIR

	cd $DTBDIR || {
		echo "Unable to cd to $DTBDIR!"
		exit 1
	}

	rm -f ./*

	echo "Processing dts files..."

	for dts in $DTSFILES; do
		echo "=> Processing: ${dts}.dts"
		${CROSS_COMPILE}cpp -nostdinc -undef -x assembler-with-cpp -I "$INCDIR" "$DTSDIR/${dts}.dts" > "${dts}.dts"
		echo "=> Generating: ${dts}.dtb"
		$DTCTOOL -p $DTB_PADDING -i "$DTSDIR" -O dtb -o "${dts}.dtb" "${dts}.dts"
	done

	echo "Generating dtb.img..."
	$RDIR/scripts/dtbTool/dtbTool -o "$OUTDIR/dtb.img" -d "$DTBDIR/" -s $PAGE_SIZE

	echo "Done."
}

FUNC_BUILD_KERNEL()
{
	echo ""
        echo "=============================================="
        echo "START : FUNC_BUILD_KERNEL"
        echo "=============================================="
        echo ""
        echo "build common config="$KERNEL_DEFCONFIG ""
        echo "build model config="$MODEL ""
	echo "build job number= " $BUILD_JOB_NUMBER ""

	FUNC_CLEAN_DTB

	make -j$BUILD_JOB_NUMBER ARCH=$ARCH \
			CROSS_COMPILE=$BUILD_CROSS_COMPILE \
			$KERNEL_DEFCONFIG || exit -1

	make -j$BUILD_JOB_NUMBER ARCH=$ARCH \
			CROSS_COMPILE=$BUILD_CROSS_COMPILE || exit -1

	FUNC_BUILD_DTIMAGE_TARGET
	
	echo ""
	echo "================================="
	echo "END   : FUNC_BUILD_KERNEL"
	echo "================================="
	echo ""
}

FUNC_BUILD_RAMDISK()
{
	mv $RDIR/arch/$ARCH/boot/Image $RDIR/arch/$ARCH/boot/boot.img-zImage
	mv $RDIR/arch/$ARCH/boot/dtb.img $RDIR/arch/$ARCH/boot/boot.img-dtb

	case $MODEL in
	gracelte)
		case $VARIANT in
		can|duos|eur|xx)
			rm -f $RDIR/ramdisk/SM-N935F/split_img/boot.img-zImage
			rm -f $RDIR/ramdisk/SM-N935F/split_img/boot.img-dtb
			mv -f $RDIR/arch/$ARCH/boot/boot.img-zImage $RDIR/ramdisk/SM-N935F/split_img/boot.img-zImage
			mv -f $RDIR/arch/$ARCH/boot/boot.img-dtb $RDIR/ramdisk/SM-N935F/split_img/boot.img-dtb
			cd $RDIR/ramdisk/SM-N935F
			./repackimg.sh --nosudo
			echo SEANDROIDENFORCE >> image-new.img
			;;
		esac
	;;
	*)
		echo "Unknown device: $MODEL"
		exit 1
		;;
	esac
}

FUNC_BUILD_ZIP()
{
	cd $RDIR/build
	rm $MODEL-$VARIANT.img
	case $MODEL in
	gracelte)
		case $VARIANT in
		can|duos|eur|xx)
			mv -f $RDIR/ramdisk/SM-N935F/image-new.img $RDIR/build/$MODEL-$VARIANT.img
			;;
		esac
	;;
	*)
		echo "Unknown device: $MODEL"
		exit 1
		;;
	esac
	echo " "
	echo "Creating flashable zip..."
	echo " "
	zip -r -x .gitignore -9 ../$KERNELNAME-$KERNEL_VERSION-N935XX.zip .
	echo " "
}

# MAIN FUNCTION
rm -rf ./build.log
(
	START_TIME=`date +%s`

	FUNC_BUILD_KERNEL
	FUNC_BUILD_RAMDISK
	FUNC_BUILD_ZIP

	END_TIME=`date +%s`
	
	let "ELAPSED_TIME=$END_TIME-$START_TIME"
	echo "Total compile time was $ELAPSED_TIME seconds"
	echo "build common config="$KERNEL_DEFCONFIG ""
	echo "ARCH = "$ARCH ""
	echo "toolchain = "$BUILD_CROSS_COMPILE ""

) 2>&1	 | tee -a ./build.log
