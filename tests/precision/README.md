# Precision test

## Introduction

This dir contains a set of automated precision tests. Each test generates a trace file and feeds it to `VANS`. These
tests are multi-threaded to save your test time.

After tests are finished, this test framework aggregates the test results and generate a report. This report is a `HTML`
file generated from `RMarkdown` code, so you may want to install `R` and `RMarkdown` if you want to view the report.

## Install

### Python

To run the tests, you need to install Python 3.6 or later (We use the `f-string` feature that was first introduced in
Python 3.6).

The additional Python packages are specified in `requirements.txt`.

### RMarkdown

To generate `HTML` report, you need to install `RMarkdown` environment:

1. Install `R` tool, using your Linux package manager.
2. Open a terminal, use the following commands to install R packages

```shell
# Enter R cli environment
$ R
# Install RMarkdown packages
$ install.packages(c("rmarkdown", "knitr"))
# Install additional R packages for plotting
$ install.packages(c("ggplot2", "ggpubr", "ggsci", "scales", "ggrepel", "viridis", "gridExtra", "dplyr"))
```

To check if your `RMarkdown` environment is ready:

```shell
# Goto template dir
$ cd template
# Generate a sample HTML file from RMarkdown template
$ Rscript -e 'rmarkdown::render("report.Rmd")'
```

This should generate a HTML file containing only a title. If the generation success, then your `RMarkdown` environment
is ready. If the generation failed, pleas open a GitHub issue and discuss the issues with the developers.

## Usage

To run the tests, execute the following command **in project root dir**:

```shell
$ python3 tests/precision/precision_test.py test/precision
```

You can modify the file `basedata.yml` to choose the test sets and change the test framework behaviour.

For each sub test, e.g. `ptr_chasing`, please modify the `metadata.yml` file under test dir to change the test cases.
