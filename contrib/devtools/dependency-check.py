#!/usr/bin/env python3
import sys
from subprocess import check_output

count = 0
full_data = []
lineCounter = 1
lib = "none"
sourceFilesCounter = 0
sourceFiles = []
depenFiles = []
bitcoinFiles = []
tempLine = ""
tempCount = 0
bitcoin = True
prev = False
out = check_output(["git", "rev-parse", "--show-toplevel"])
# Strips unnecessary arguments
directory = str(out).strip('b').strip('\'').lstrip('\'').strip('n').strip('\\') + "/src/"
totalDependCounter = 0
file = open(directory + "Makefile.am", "r")
data = file.read()
lines = data.split("\n")
# Asks user what dependency they want
depen = input("Enter the Dependency: ").lower()
print("")

for row in lines:
    # Collapse multiple lines into one
    tempLine += row
    makeFileCounter = sourceFilesCounter
    oldlib = lib
    depenFiles = []
    dependCounter = 0
    # Bitcoin Core Header files
    if tempLine.count("BITCOIN_CORE_H = \\"):
        sourceFilesCounter = 0
        lib = tempLine[:-4]
    # Line contains SOURCES = \
    if tempLine.count("SOURCES = \\"):
        sourceFilesCounter = 0
        lib = tempLine[:-14]
    # Adds the last library to the script
    if tempLine.count("EXTRA_DIST"):
        sourceFilesCounter = 0

    else:
        # Adds file to list of things that needs to be checked excluding unnecessary files
        if tempLine.count("\\") > 0 and tempLine.count("$") == 0 and \
                tempLine.count("=") == 0 and tempLine.count("#") == 0:
            sourceFiles.append(str(tempLine).strip('\\').strip(' '))
            sourceFilesCounter += 1

            # Done with this multiline, clear tempLine
            tempLine = ""
            tempCount = 0
            prev = True

        else:
            # Adds last file to the list
            if prev is True and tempLine.count("#") == 0:
                # If dependent on bitcoin core files adds all bitcoin core files to the list
                if tempLine.count("$(BITCOIN_CORE_H)"):
                    prev = False
                    for files in bitcoinFiles:
                        sourceFiles.append(files)
                else:
                    sourceFiles.append(str(tempLine).strip())
                    sourceFilesCounter += 1
                    prev = False

            tempCount += 1

    # Checks if finished with old library and prints out the old library's files
    if sourceFilesCounter == 0 and makeFileCounter != 0:
        # Scans files for #include and checks if dependency is in same line
        for files in sourceFiles:
            with open(directory + files) as f:
                addList = True
                for line in f:
                    if '#include' in line:
                        if depen in line:
                            if addList is True:
                                depenFiles.append(files)
                                dependCounter += 1
                                addList = False
                                # Adds file to list of Bitcoin Core Files that are used in other libraries
                                if bitcoin is True:
                                    bitcoinFiles.append(files)
        bitcoin = False
        list(set(depenFiles))
        print("Number of Files in " + oldlib + " with " + depen + ": " + str(dependCounter))
        [print(file) for file in depenFiles]
        print("")
        totalDependCounter += dependCounter
        sourceFiles = []
        lib = tempLine[:-14]

    # Clear tempLine
    tempLine = ""
    tempCount = 0
    lineCounter += 1

print("# of files with dependency: " + str(totalDependCounter))

sys.exit(totalDependCounter)
