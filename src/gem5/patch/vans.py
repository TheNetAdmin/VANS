# -*- mode:python -*-
from m5.params import *
from m5.objects.AbstractMemory import *

class VANS(AbstractMemory):
    type = 'VANS'
    cxx_header = 'mem/vans.hh'

    port = ResponsePort("This port sends responses and receives requests")

    config_path = Param.String("", "config dir path")
