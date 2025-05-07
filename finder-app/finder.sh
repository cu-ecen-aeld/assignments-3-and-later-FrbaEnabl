#!/bin/sh

filesdir=$1
searchstr=$2

if [ $# -ne 2 ]
then 
    echo "2 arguments must be specified! Exiting..."
    exit 1
fi

if [ ! -d "$filesdir" ]
then 
    echo "File directory does not exist! Exiting..."
    exit 1
fi

# Use find to count files matching the pattern
found_files=$(find "$filesdir" -type f | wc -l)

# Use grep to count the number of matching lines, searching recursively in the specified directory
found_lines=$(grep -r "$searchstr" "$filesdir" | wc -l)
echo "check $found_files files"
echo "check $found_lines lines"
echo "The number of files are $found_files and the number of matching lines are $found_lines"
