#!/bin/bash

sudo rm -f *.diff *.out

libdir="$(realpath "../.libs")"
ldpath="LD_LIBRARY_PATH=$libdir"
BSDIFF="sudo $ldpath valgrind -q $libdir/bsdiff"
BSPATCH="sudo $ldpath valgrind -q $libdir/bspatch"

echo -n "5.."
$BSPATCH data/5.bspatch.original 5.out data/5.bspatch.diff
echo -n "6.."
$BSPATCH data/6.bspatch.original 6.out data/6.bspatch.diff
echo -n "7.."
$BSPATCH data/7.bspatch.original 7.out data/7.bspatch.diff
echo -n "8.."
$BSPATCH data/8.bspatch.original 8.out data/8.bspatch.diff
echo -n "9.."
$BSPATCH data/9.bspatch.original 9.out data/9.bspatch.diff
diff data/9.bspatch.modified 9.out
if [ $? -ne 0 ]
then
	echo "bspatch 9 output does not match expected!!"
fi
echo -n "10.."
$BSPATCH data/10.bspatch.original 10.out data/10.bspatch.diff
diff data/10.bspatch.modified 10.out
if [ $? -ne 0 ]
then
	echo "bspatch 10 output does not match expected!!"
fi
#same as 9 but with zeros encoding
echo -n "11.."
$BSPATCH data/9.bspatch.original 11.out data/11.bspatch.diff
diff data/9.bspatch.modified 11.out
if [ $? -ne 0 ]
then
	echo "bspatch 11 output does not match expected!!"
fi
echo -n "12.."
$BSPATCH data/12.bspatch.original 12.out data/12.bspatch.diff
diff data/12.bspatch.modified 12.out
if [ $? -ne 0 ]
then
	echo "bspatch 12 output does not match expected!!"
fi
echo -n "13.."
$BSDIFF data/13.bspatch.original data/13.bspatch.modified 13.diff any
$BSPATCH data/13.bspatch.original 13.out 13.diff
diff data/13.bspatch.modified 13.out
if [ $? -ne 0 ]
then
	echo "bspatch 13 output does not match expected!!"
fi

# Next a very loooong running test, but one which successfully condenses the 2MB
# original file pair into a 26kB bsdiff.  The bsdiff computation alone (ie:
# non-valgrind'd) takes ~20minutes on a decent build machine.  Running it
# through valgrind takes many many hours to run to completion.  Therefore leave
# filepair #14 as one for only occasional use in long-running regression
# testing.  The other file pairs can be check quickly enough that they can be
# used in a regression test run at every check-in of code changes to the bsdiff
# implementation.
#
#echo -n "14.."
#$BSDIFF data/14.bspatch.original data/14.bspatch.modified 14.diff any
#$BSPATCH data/14.bspatch.original 14.out 14.diff
#diff data/14.bspatch.modified 14.out
#if [ $? -ne 0 ]
#then
#	echo "bspatch 14 output does not match expected!!"
#fi

echo -n "15.."
$BSDIFF data/15.bspatch.original data/15.bspatch.modified 15.diff any
# expected output: "Failed to create delta (-1)"
if [ $? -ne 255 ]
then
	echo "bspatch 15 creation has memory management issue!"
fi

echo -n "16.."
# any valgrind errors may indicate a buffer overflow
$BSPATCH data/16.bspatch.original 16.out data/16.bspatch.diff

# add final newline
echo ""
