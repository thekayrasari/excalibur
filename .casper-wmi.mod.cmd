savedcmd_casper-wmi.mod := printf '%s\n'   casper-wmi.o | awk '!x[$$0]++ { print("./"$$0) }' > casper-wmi.mod
