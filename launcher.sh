#!/bin/bash

DATE=$(date +"%Y-%m-%d-%Hh%Mmin%Ssec")
TOP=/lus/eagle/projects/Diaspora/ndhai/aps-mini-apps/failure-injection/periodic/D${DATE}/
mkdir -p $TOP
echo $TOP > recent-run
cd $TOP
qsub -o $TOP $HOME/diaspora/src/aps-mini-apps/polaris-test-failure.sh

