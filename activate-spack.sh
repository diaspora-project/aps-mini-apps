# This script activates the Spack environment for APS
#!/bin/bash

# SETUP INSTRUCTIONS:
# 1. Clone the original git repo:
#   git clone https://github.com/ngduchai/spack-aps.git
# 2. Replace this with the actual path to your cloned spack-aps directory
SPACK_PATH=/path/to/spack-aps

# Check if the Spack APS directory exists
if [ ! -d "$SPACK_PATH" ]; then
    echo "Error: Spack APS directory not found at $SPACK_PATH. Please clone the repository first."
    exit 1
fi

# Source the Spack environment setup script
if [ -f "$SPACK_PATH/share/spack/setup-env.sh" ]; then
    . "$SPACK_PATH/share/spack/setup-env.sh"
else
    echo "Error: setup-env.sh not found in $SPACK_PATH/share/spack. Please check your Spack installation."
    exit 1
fi

# Activate the APS environment
if ! spack env activate APS; then
    echo "Error: Failed to activate the APS Spack environment."
    exit 1
fi

echo "Spack APS environment activated successfully."
