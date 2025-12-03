#import pdb; pdb.set_trace()
import re
import numpy as np
import matplotlib.pyplot as plt

# Globals used to build a data structure.
#   * signal_names[] is a list of the names from line #1 of the main input
#     file.
#   * values[] is a list of numpy arrays; one array for each signal. So
signal_names=[]         # List of the signals we traced, grabbed from line 1
                        # of the input file
values=None             # List of np arrays, one array per signal.

# Parse the input file.
# - Read 'filename' (which should be a file of dumped signal values from
#   lab7_host_main.cxx). The first line of 'filename' is a list of the signals
#   that were traced/dumped. Each of the remaining lines is a list of the
#   dumped values for those signals.
# - Build a data structure from reading 'filename'. The data structure is
#   described above.
# You shouldn't have to touch this routine.
def parse_inputfile (filename):
    global signal_names, values
    infile = open (filename, "r")
    first = True
    for line in infile:
        fields = re.split (r"\s+", line.strip())
        if (first):
            # The top line has the names of the signals we've dumped, such as
            # "input filtered squared".
            first = False
            signal_names = fields
            # For three signal names, values will be a list of 3 empty lists.
            # We'll then populate those lists for each line we read.
            values = [ [] for f in fields]
            continue

        # All lines other than the top one have a whitespace-separated list
        # of signals, such as "12  5  2". Three values would mean we have three
        # signals; append each signal to one of our three lists.
        numbs = [float(f) for f in fields]
        assert len(numbs) == len(values), "Incorrect number of numbers"
        for idx,n in enumerate(numbs):
            values[idx].append(n)

    # Convert the lists to arrays. Now 'values' is a list of numpy arrays.
    for idx in range(len(values)):
        values[idx] = np.array(values[idx])

def plot_what_you_want():
    print("plotting...")

    #plot_signal("sample", 1, -2000)
    #plot_signal("notch60")
    #plot_signal("abs_val")
    #plot_signal("ttm")
    #plot_signal("lp35")
    #plot_signal("thresh")
    

    n_pts = values[0].size
    x_axis = np.arange(n_pts, dtype=float) * .002
    plt.plot(x_axis, np.zeros_like(values[0]))

    plt.legend(loc="upper right")
    plt.show()
    plt.savefig("lab7_plot.png", dpi=300, bbox_inches="tight")
    print("Saved plot to lab7_plot.png")

# Plot a signal by its name, and allow scaling and translation.
# So, plot signame*times + plus.
# The optional 'spacing' parameter tells how many seconds between data points;
# i.e., what the sampling time was in lab #6.
def plot_signal (signame, times=1, plus=0, spacing=.002):
    idx = signal_names.index(signame)
    n_pts = values[idx].size
    x_axis = np.arange (n_pts,dtype=float) * spacing
    data = values[idx]*times + plus
    plt.plot (x_axis, data, label=signame, marker=".")

parse_inputfile ("run.out")
plot_what_you_want()