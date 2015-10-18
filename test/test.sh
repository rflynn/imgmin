#!/bin/bash

IMGMIN=../src/imgmin

if [ ! -e $IMGMIN ]
then
    echo 'Build imgmin first!'
    exit 1
fi

UNAME="$(uname)"
case "$UNAME" in
"Darwin")
    filesize()
    {
        stat -f%z "$1"
    }
    ;;
*)
    filesize()
    {
        echo $(du -b "$1" | cut -f1)
    }
    ;;
esac

pct_less()
{
    before=$1
    after=$2
    diff=$(($before-$after))
    echo $((100-(((($before-$diff)*100)/$before))))
}

echo "Ensure that savings are always [0%,100%)"

rm -f after-*.{jpg,gif,png} 2>/dev/null #clear previous output

beforetotal=0
aftertotal=0
for before in $(find ../examples | grep -E "\.(png|gif|jpg)" | grep -v -- "-after" | sort)
do
    echo "test $before " | tr -d '\n'
    after="after-"$(basename "$before")
    out=$($IMGMIN "$before" "$after" 2>&1)
    beforebytes=$(filesize "$before")
    afterbytes=$(filesize "$after")
    totalbefore=$(($totalbefore+$beforebytes))
    totalafter=$(($totalafter+$afterbytes))
    if [ $afterbytes -gt $beforebytes ]
    then
        echo "FAIL"
        echo "before:$beforebytes after:$afterbytes"
        echo "$out"
    else
        pct_saved=$(pct_less $beforebytes $afterbytes)
        echo "ok ($pct_saved%)"
    fi
done

total_pct_saved=$(pct_less $totalbefore $totalafter)
printf "Before: %5.0fk\n" $(($totalbefore/1024))
printf "After:  %5.0fk (%d%% saved)\n" \
    $(($totalafter/1024)) \
    $total_pct_saved

