#!/bin/bash
LOG_FILE={{log_dir}}/{{roslaunch_script}}.log

echo "" >> ${LOG_FILE}
echo "" >> ${LOG_FILE}
echo "" >> ${LOG_FILE}
echo "" >> ${LOG_FILE}
echo "#############################################" >> ${LOG_FILE}
echo "Running {{roslaunch_script}}" >> ${LOG_FILE}
echo $(date) >> ${LOG_FILE}
echo "#############################################" >> ${LOG_FILE}
echo "" >> ${LOG_FILE}
echo "Logs:" >> ${LOG_FILE}

set -e
set -v

{

source {{source_path}}; roslaunch ros_homebot all.launch

} &>> ${LOG_FILE}