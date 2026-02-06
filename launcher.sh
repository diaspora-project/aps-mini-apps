#!/bin/bash
PROJ=$PWD
DATE=$(date +"%Y-%m-%d-%Hh%Mmin%Ssec")
TOP=/lus/eagle/projects/Diaspora/ndhai/aps-mini-apps/scaling/D${DATE}/
mkdir -p $TOP
rsync -av --exclude='.git' --exclude='build' --exclude='data' --exclude='tests' --exclude='runinfo' $PROJ $TOP
echo $TOP > recent-run
cd $TOP/aps-mini-apps
qsub -o $TOP/aps-mini-apps polaris-test-scale.sh

