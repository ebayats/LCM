#! /usr/bin/env python3

import sys
import os
import re
from subprocess import Popen

result = 0

######################
# Test 1 - NoVolJ
######################
print "test 1 - NoVolJ"
name = "NoVolJ"
log_file_name = name + ".log"
if os.path.exists(log_file_name):
    os.remove(log_file_name)
logfile = open(log_file_name, 'w')

# run Albany
command = ["./Albany", "input" + name + ".yaml"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code

# run exodiff
command = ["./exodiff", "-stat", "-f", \
           name + ".exodiff", \
           "out" + name + ".gold.e", \
           "out" + name + ".e"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code

if result != 0:
    print "result is %s" % result
    print "%s test has failed" % name
    sys.exit(result)

with open(log_file_name, 'r') as log_file:
    print log_file.read()


######################
# Test 2 - VolJ
######################
print "test 2 - VolJ"
name = "VolJ"
log_file_name = name + ".log"
if os.path.exists(log_file_name):
    os.remove(log_file_name)
logfile = open(log_file_name, 'w')

# run Albany
command = ["./Albany", "input" + name + ".yaml"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code

# run exodiff
command = ["./exodiff", "-stat", "-f", \
           name + ".exodiff", \
           "out" + name + ".gold.e", \
           "out" + name + ".e"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code
if result != 0:
    print "result is %s" % result
    print "%s test has failed" % name
    sys.exit(result)
with open(log_file_name, 'r') as log_file:
    print log_file.read()


######################
# Test 3 - Average Pressure
######################
print "test 3 - Average Pressure"
name = "AveragePressure"
log_file_name = name + ".log"
if os.path.exists(log_file_name):
    os.remove(log_file_name)
logfile = open(log_file_name, 'w')

# run Albany
command = ["./Albany", "input" + name + ".yaml"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code

# run exodiff
command = ["./exodiff", "-stat", "-f", \
           name + ".exodiff", \
           "out" + name + ".gold.e", \
           "out" + name + ".e"]
p = Popen(command, stdout=logfile, stderr=logfile)
return_code = p.wait()
if return_code != 0:
    result = return_code
if result != 0:
    print "result is %s" % result
    print "%s test has failed" % name
    sys.exit(result)
with open(log_file_name, 'r') as log_file:
    print log_file.read()


######################
# Test 4 - Membrane Forces
######################

sys.exit(result)
