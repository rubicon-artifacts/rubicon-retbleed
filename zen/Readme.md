# Rubicon-Enhanced Retbleed on AMD EPYC 7252

This directory contains the Rubicon-enhanced Retbleed attack on AMD EPYC 7252 processors. It builds upon the original Retbleed attack, introducing additional functionality for enhanced exploitation.

## Prerequisites
Before proceeding, ensure you have reviewed the [AMD-specific README](https://github.com/comsec-group/retbleed/tree/master/retbleed_zen) in the original [Retbleed repository](https://github.com/comsec-group/retbleed). Follow the setup instructions provided there.

## Building the Project
To compile the code, execute the following command from this directory:
```bash
make -C ../massaging
make
```

## Supported Modes
The Rubicon-enhanced Retbleed attack supports the following modes:

- **Baseline with THPs**: The original Retbleed attack with Transparent Huge Pages (THPs) enabled.
- **Baseline without THPs**: The original Retbleed attack with THPs disabled.
- **Rubicon without THPs**: The Rubicon-enhanced Retbleed attack with massaging functionality.

### Configuring Modes
- **Enable THPs**: Define `ALLOW_THP` to enable Transparent Huge Pages.
- **Disable Rubicon Massaging**: Set the `should_massage` variable in `retbleed.c` to `0`.

## Running the Attack
After configuring the desired mode, execute the attack by running:
```bash
./fullchain.sh
```

## Additional Notes
Depending on your system configuration, you may need to modify line 403 in `retbleed.c` to ensure that Retbleed searches for the correct string in physical memory. On some systems, the target string might be `root:$`, while on others it could be `root:*` or another variation. Update this line to match the string format used by your system.