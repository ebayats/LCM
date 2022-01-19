
#! /usr/bin/env python3

import sys
import os
import re
from subprocess import Popen

result = 0

name = "cube"
log_file_name = name + ".log"

with open(log_file_name, 'r') as log_file:
    print(log_file.read())

#specify tolerance to determine test failure / passing
tolerance = 1.0e-12
meanvalue = 0.0

for line in open(log_file_name):
  if "Main_Solve: MeanValue of final solution" in line:
    s = line
    s = line[40:]
    d = float(s)
    print(d)
    if (d > meanvalue + tolerance or d < meanvalue - tolerance):
      result = result+1

if result != 0:
    print("result is %s" % result)
    print("%s test has failed" % name)
    sys.exit(result)

