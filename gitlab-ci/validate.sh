#!/bin/bash

set -e

# handle commandline options
usage()
{
   echo "Script for validate RPM packages with PSDK."
   echo
   echo "Usage: validate.sh <options>"
   echo
   echo "Options:"
   echo "-p | --profile     Validation profile for signing RPM-package."
   echo "                   Available validation profiles: regular, extended, mdm, antivirus, auth."
   echo "-h | --help        Print this help and exit."
}

while [[ ${1:-} ]]; do
    case "$1" in
    -p | --profile ) shift
        OPT_SIGN_PROFILE=$1; shift
        ;;
    -h | --help ) shift
        usage
        exit;;
    * )
        usage
        exit;;
    esac
done

if [[ -z $OPT_RPM_PATH ]]; then
    echo "Enter path to directory with RPM packages"
    exit 1
fi

if [[ -z $OPT_SIGN_PROFILE ]]; then
    OPT_SIGN_PROFILE="regular"
fi

if [[ "$OPT_SIGN_PROFILE" != "regular" ]] && [[ "$OPT_SIGN_PROFILE" != "extended" ]] &&
[[ "$OPT_SIGN_PROFILE" != "mdm" ]] && [[ "$OPT_SIGN_PROFILE" != "antivirus" ]] && [[ "$OPT_SIGN_PROFILE" != "auth" ]]; then
    echo "Incorrect validation profile"
    exit 1
fi

# Validate artifact
echo "Validate artifact"
rpm-validator \
--profile $OPT_SIGN_PROFILE \
$OPT_RPM_PATH/*