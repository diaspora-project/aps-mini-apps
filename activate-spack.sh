# git clone https://github.com/ngduchai/spack-aps.git
# then replace it with the actual path to your cloned spack-aps directory
# This script activates the Spack environment for APS
#!/bin/bash
# Check if the Spack APS directory exists
if [ ! -d "/path/to/spack-aps" ]; then
    echo "Spack APS directory not found. Please clone the repository first."
    exit 1
fi
 . /path/to/spack-aps/share/spack/setup-env.sh;
 spack env activate APS
