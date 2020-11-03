from pathlib import Path


def get_code_file(file: Path):
    result = file.open().readlines()
    result = [x.rstrip() for x in result]
    return result


def form_code_region(code_content: list):
    result = ["```{r warning=FALSE, message=FALSE}"]
    result += code_content
    result += ["```\n"]
    return result
