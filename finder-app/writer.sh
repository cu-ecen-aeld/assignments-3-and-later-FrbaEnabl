#!/bin/bash

writefile=$1
writestr=$2

if [ $# -ne 2 ]
then 
    echo "2 arguments must be specified! Exiting..."
    exit 1
fi

if [ ! -f "$writefile" ]
then 
    echo "File does not exist, creating file $writefile"
    mkdir -p "$(dirname "$writefile")"
fi

# Use cat to write file
echo "$writestr" > "$writefile"

if [ $? -ne 0 ]
then
    echo "Write command did not exit succesfully! Exiting..."
    exit 1
fi