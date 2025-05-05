# Rubicon-Enhanced Retbleed

This repository contains the implementation of the Rubicon-enhanced Retbleed attack. This attack exploits the Retbleed vulnerability in Intel and AMD processors, leveraging the Rubicon framework to precisely position `/etc/shadow` at a known physical memory location. By doing so, it eliminates the need for the lengthy scanning phase present in the original Retbleed attack, enabling direct access to sensitive data.

## Repository Structure

The repository is organized as follows:

- **`massage/`**: Contains the implementation of the Rubicon framework, which is responsible for positioning `/etc/shadow` at a known physical memory location.
- **`intel/`**: Includes the code for the Rubicon-enhanced Retbleed attack targeting Intel processors.
- **`zen/`**: Includes the code for the Rubicon-enhanced Retbleed attack targeting AMD processors.
- **`kmod_read_mem/`**: Contains the kernel module and user-space program used to read the physical address of `/etc/shadow` for validating experimental results.

## Citing our Work
To cite Rubicon in academic papers, please use the following BibTeX entry:

```
@inproceedings{boelcskei_rubicon_2025,
	title = {{Rubicon: Precise Microarchitectural Attacks with Page-Granular Massaging}}, 
	url = {Paper=https://comsec.ethz.ch/wp-content/files/rubicon_eurosp25.pdf},
	booktitle = {{EuroS\&P}},
	author = {Bölcskei, Matej and Jattke, Patrick and Wikner, Johannes and Razavi, Kaveh},
	month = jun,
	year = {2025},
	keywords = {dir\_os, type\_conf}
}
```

For more details, refer to the [paper](https://comsec.ethz.ch/wp-content/files/rubicon_eurosp25.pdf).