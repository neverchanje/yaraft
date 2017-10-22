#!/usr/bin/env bash

# format each of the modified files comparing to the latest git version.
git status -s | while read LINE; do
    IFS=' '
    PARAMS=(${LINE})
    if [[ ${#PARAMS[@]} == 2 ]]; then
        if [[ ${PARAMS[0]} == 'M' || ${PARAMS[0]} == 'AM' ]]; then
            FILE=${PARAMS[1]}
            EXTENSION=${FILE##*.}
            if [[ ${EXTENSION} == 'h' || ${EXTENSION} == 'cc' || ${EXTENSION} == 'cpp' ]]; then
                clang-format -i ${FILE}
            fi
        fi
    fi
done