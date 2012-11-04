#!/bin/bash
LOC=$(pwd | awk -F '/' 'BEGIN { OFS = "/" } { if ( $2 == "Users" ) { print $1, $2, $3 }}')
if [[ $LOC != "" ]] ; then
    TARGET=$(mount | grep $LOC)
    if [[ $TARGET != "" ]] ; then
	NOSUID=$(echo $TARGET | grep "nosuid")
	if [[ $NOSUID != "" ]] ; then
	    echo protected
	fi
    fi
fi
echo "."
