#!/bin/bash

IMGMIN=../src/imgmin

if [ ! -e $IMGMIN ]
then
    echo 'Build imgmin first!'
    exit 1
fi

filesize()
{
    echo $(du -b "$1" | cut -f1)
}

echo "imgmin is designed to reduce filesize while retaining quality."
echo "At worst, we should fallback to the original file."
echo "Ensure that we never *increase* filesize."

for before in $(find ../examples | grep -E "\.(png|jpg)" | grep -v -- "-after")
do
    echo "test $before " | tr -d '\n'
    after="after-"$(basename "$before")
    out=$($IMGMIN "$before" "$after" 2>&1)
    beforesize=$(filesize "$before")
    aftersize=$(filesize "$after")
    if [ $aftersize -gt $beforesize ]
    then
        echo "FAIL"
        echo "before:$beforesize after:$aftersize"
        echo "$out"
    else
        echo "ok"
        rm -f "$after"
    fi
done

