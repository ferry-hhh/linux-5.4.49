# Linux5.4.49 for CXL-DMSim

[CXL-DMSim](https://github.com/ferry-hhh/CXL-DMSim) is a full-system CXL disaggregated memory simulator based on gem5. This repository extends Linux 5.4.49 by adding a custom driver for a CXL device and modifying parts of the NUMA code to enable the CXL device to be recognized as a CPU-less NUMA node. The corresponding patch can be found in the releases.  

**Note:**  

- To enable the CXL driver, configure `CONFIG_CXL_MEM=y` or `CONFIG_CXL_MEM=m`.  
- To enable NUMA, ensure all NUMA-related options are configured.  

We provide a `cxl.config` file as a configuration reference.  

-------------

Linux kernel

============



There are several guides for kernel developers and users. These guides can

be rendered in a number of formats, like HTML and PDF. Please read

Documentation/admin-guide/README.rst first.



In order to build the documentation, use ``make htmldocs`` or

``make pdfdocs``.  The formatted documentation can also be read online at:



  https://www.kernel.org/doc/html/latest/



There are various text files in the Documentation/ subdirectory,

several of them using the Restructured Text markup notation.



Please read the Documentation/process/changes.rst file, as it contains the

requirements for building and running the kernel, and information about

the problems which may result by upgrading your kernel.