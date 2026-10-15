#include "helpdialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {

const char *kHelpHtml = R"HTML(
<h2>Flash Memory Fix &ndash; help</h2>
<p>Finds USB sticks and SD cards connected to this PC, tells you what is wrong with them, and repairs them by
cleaning, partitioning and formatting.</p>

<h3>Quick start</h3>
<ol>
<li>Run the program <b>as administrator</b> (Windows asks for permission at start).</li>
<li>Plug in the stick or card. The list updates by itself; <b>Refresh</b> forces it.</li>
<li>Click the device. Its row turns <span style="background:#b7efc5;">&nbsp;light green&nbsp;</span> and the
<b>Diagnosis</b> box explains what is wrong.</li>
<li>Click <b>Clean + Partition + Format...</b>, choose the options and click <b>Next...</b>.</li>
<li>Read the list of steps, type the disk number and click <b>Erase and continue</b>.</li>
<li>Wait for <code>RESULT: OK</code> and <code>VERIFIED</code> in the log. The drive is ready.</li>
</ol>

<h3>The device list</h3>
<ul>
<li><b>Disk</b> is the Windows disk number. <b>Status</b> shows the main problem:
<i>No partitions</i>, <i>Read-only</i>, <i>Offline</i>, <i>Not initialized</i>, <i>Partitions unreadable</i> or <i>OK</i>.</li>
<li>Only USB, SD and MMC devices can be changed. Internal disks are hidden; tick
<b>Also show internal disks</b> to see them greyed out, view only. The disk that holds Windows can never be changed.</li>
<li>Below the list: details of the selected device and its partitions.</li>
</ul>

<h3>Buttons</h3>
<ul>
<li><b>Clear read-only / bring online</b> &ndash; removes the software write-protect flag and brings an offline disk
online. Nothing is erased.</li>
<li><b>Clean + Partition + Format...</b> &ndash; does everything in one step and <b>erases all data</b>:
<ol>
<li>erases old partitions (only when there are any; an empty disk is not cleaned, because some sticks refuse that);</li>
<li>creates one partition;</li>
<li>formats it and assigns a drive letter;</li>
<li>checks that Windows mounted it, and optionally copies a file onto it.</li>
</ol></li>
<li><b>Save log...</b> &ndash; saves the log pane to a text file.</li>
</ul>

<h3>Options in the format dialog</h3>
<table cellpadding="3">
<tr><td><b>File system</b></td><td>FAT32, exFAT or NTFS (see below).</td></tr>
<tr><td><b>Partition style</b></td><td>MBR works everywhere and is right for sticks and cards. GPT only for very large drives or special needs.</td></tr>
<tr><td><b>Volume label</b></td><td>Name shown in Explorer. Letters, digits, space, _ and -. Max 11 characters (FAT32, exFAT), 32 (NTFS).</td></tr>
<tr><td><b>Partition size</b></td><td>The whole disk, or a size in MB. FAT32 is capped at 32,000 MB.</td></tr>
<tr><td><b>Allocation unit</b></td><td>Cluster size. Leave on Default unless a device asks for a specific size.</td></tr>
<tr><td><b>Quick format</b></td><td>Fast. Untick for a full format, which is much slower.</td></tr>
<tr><td><b>Overwrite with zeros</b></td><td>Rewrites every sector. Very slow (can take hours); normally not needed.</td></tr>
<tr><td><b>Copy a file</b></td><td>Puts a file on the new drive afterwards and, if ticked, reads it back and compares its SHA-256.</td></tr>
</table>

<h3>Which file system?</h3>
<ul>
<li><b>FAT32</b> &ndash; works with almost every device (instruments, cameras, car stereos, firmware updates).
Windows can only format it up to 32 GB, and files must be smaller than 4 GiB. On a larger stick the app creates a
32,000 MB partition and leaves the rest unused.</li>
<li><b>exFAT</b> &ndash; for big drives and files over 4 GiB; fine on modern PCs, Macs and cameras, but not on every older device.</li>
<li><b>NTFS</b> &ndash; Windows drives only.</li>
</ul>
<p>Windows shows only the <b>first partition</b> of a removable USB stick.</p>

<h3>Troubleshooting</h3>
<ul>
<li><b>Drive shows 0 B, "Unknown capacity", or "Windows was unable to complete the format"</b> &ndash; the stick usually
has no partition. Use <b>Clean + Partition + Format</b>.</li>
<li><b>"Access is denied" while cleaning</b> &ndash; unplug the stick, plug it back in <i>alone</i>, and run it again.
On a disk without partitions the app skips the clean step.</li>
<li><b>Format keeps failing, or the log shows I/O errors</b> &ndash; the stick may be failing or have fake capacity.
Test it with a tool such as <i>f3</i> or <i>H2testw</i> before trusting it with data.</li>
<li><b>Device not in the list</b> &ndash; try another port, press Refresh, and make sure a card is inserted in the
reader. Only USB, SD and MMC devices are listed.</li>
<li><b>Buttons are greyed out</b> &ndash; the program is not running as administrator, the selected disk is an internal
or system disk, or a job is still running.</li>
<li><b>The log says a command failed</b> &ndash; the log shows diskpart's own message. Use <b>Save log...</b> if you
need to ask for help.</li>
</ul>

<h3>Safety</h3>
<ul>
<li>Every erase needs you to type the disk number, and shows the exact diskpart script first.</li>
<li>Right before running, the program checks again that the device is still the same one (model, size, serial).</li>
<li>Start the program with <code>--dry-run</code> to see the scripts without running anything.</li>
</ul>
)HTML";

}  // namespace

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("Help - Flash Memory Fix"));
    resize(760, 680);

    auto *lay = new QVBoxLayout(this);
    auto *view = new QTextBrowser;
    view->setOpenExternalLinks(true);
    view->setHtml(QString::fromUtf8(kHelpHtml));
    lay->addWidget(view);

    auto *box = new QDialogButtonBox(QDialogButtonBox::Close);
    lay->addWidget(box);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
}
