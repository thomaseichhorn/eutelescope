#!/bin/sh

##################################
#                                #
# ALiBaVa Analysis - Simulation  #
#                                #
# GBL alignment - AllPix data    #
# This runs the analysis chain   #
#                                #
# x_sim_pedestal.sh and          #
# x_sim_tel should be called     #
# before...                      #
#                                #
##################################

jobsub.py -c config.cfg -csv runlist.csv simconverter $1
jobsub.py -c config.cfg -csv runlist.csv reco $1
jobsub.py -c config.cfg -csv runlist.csv clustering-1 $1
jobsub.py -c config.cfg -csv runlist.csv clustering-2 $1
jobsub.py -c config.cfg -csv runlist.csv datahisto $1
jobsub.py -c config.cfg -csv runlist.csv merge $1
jobsub.py -c config.cfg -csv runlist.csv hitmaker $1
jobsub.py -c config.cfg -csv runlist.csv coordinator $1

jobsub.py -c config.cfg -csv runlist.csv alignGBL1 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL2 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL3 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL4 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL5 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL6 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL7 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL8 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL9 $1
jobsub.py -c config.cfg -csv runlist.csv alignGBL10 $1

jobsub.py -c config.cfg -csv runlist.csv fitGBL $1

#
