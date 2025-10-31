#Dex-Optimizer

This module is designed specifically for alioth devices.

Runs dexopt command automatically when phone is charging and charge is 100%.

## What is dexopt?

It optimizes app by translating the app's dalvik bytecode to machine code which reduces overhead.

##Benefits 

- Improves app's startup time.
- Improvement in Battery Backup.

## Usage

- Install this module
- dexoptimizer.log file is created and timestamps after dexopt command is completed will be appended to the log file.

#Note
- This Module is not open source,this code is compiled to c binary to reduce battery usage and to access kernel parameters.
