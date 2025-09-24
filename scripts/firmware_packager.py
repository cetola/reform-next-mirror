#!/usr/bin/python3
#
# Copyright (C) 2017 Max Ehrlich maxehr@gmail.com
#
# SPDX-License-Identifier: LGPL-2.1+
#

import argparse
import subprocess
import contextlib
import os
import shutil
import tempfile
import time
from pathlib import Path
from xml.sax.saxutils import escape as escxml


@contextlib.contextmanager
def cd(path):
    prev_cwd = os.getcwd()
    os.chdir(path)
    yield
    os.chdir(prev_cwd)


firmware_metainfo_template = """<?xml version="1.0" encoding="UTF-8"?>
<component type="firmware">
  <id>{firmware_id}</id>
  <name>{firmware_name}</name>
  <summary>{firmware_summary}</summary>
  <description>
    {firmware_description}
  </description>
  <provides>
    <firmware type="flashed">{device_guid}</firmware>
  </provides>
  <url type="homepage">{homepage}</url>
  <metadata_license>CC0-1.0</metadata_license>
  <project_license>{firmware_license}</project_license>
  <updatecontact>{contact_info}</updatecontact>
  <developer_name>{developer_name}</developer_name>
  <releases>
    <release version="{release_version}" timestamp="{timestamp}">
      <url type="source">{release_source_url}</url>
      <description>
        {release_description}
      </description>
    </release>
  </releases>
  <categories>
    <category>{category}</category>
  </categories>
  <custom>
    <value key="LVFS::VersionFormat">{version_format}</value>
    <value key="LVFS::UpdateProtocol">{update_protocol}</value>
    <value key="LVFS::DeviceIntegrity">{device_integrity}</value>
  </custom>
</component>
"""


def make_firmware_metainfo(firmware_info, dst):
    local_info = vars(firmware_info)
    if args.release_description_file:
        local_info["release_description"] = Path(args.release_description_file).read_text()
    del local_info["release_description_file"]

    local_info = {
        key: value if key == 'release_description' or value is None else escxml(value)
        for key, value in local_info.items()
    }
    firmware_metainfo = firmware_metainfo_template.format(
        **local_info, timestamp=int(time.time())
    )

    with open(os.path.join(dst, "firmware.metainfo.xml"), "w") as f:
        f.write(firmware_metainfo)


def get_firmware_bin(root, bin_path, dst):
    with cd(root):
        shutil.copy(bin_path, os.path.join(dst, "firmware.bin"))


def create_firmware_cab(folder):
    with cd(folder):
        command = [
            "gcab",
            "--create",
            "firmware.cab",
            "firmware.bin",
            "firmware.metainfo.xml",
        ]
        subprocess.check_call(command)


def main(args):
    with tempfile.TemporaryDirectory() as d:
        print("Using temp directory {}".format(d))

        print("Locating firmware bin")
        get_firmware_bin(d, args.bin, d)

        print("Creating metainfo")
        make_firmware_metainfo(args, d)

        print("Creating cabinet file")
        create_firmware_cab(d)

        print("Done")
        shutil.copy(os.path.join(d, "firmware.cab"), args.out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Create fwupd package"
    )
    parser.add_argument(
        "--firmware-id",
        help="ID of the firmware package",
        required=True,
    )
    parser.add_argument(
        "--firmware-name",
        help="Name of the firmware package",
        required=True,
    )
    parser.add_argument(
        "--firmware-license",
        help="SPDX License Identifier",
    )
    parser.add_argument(
        "--firmware-summary", help="One line description of the firmware package"
    )
    parser.add_argument(
        "--firmware-description", help="Longer description of the firmware package"
    )
    parser.add_argument(
        "--device-guid",
        help="GUID of the device this firmware will run on, this *must* match the output of one of the GUIDs in `fwupdmgr get-devices`",
        required=True,
    )
    parser.add_argument(
        "--device-integrity",
        help="Device Integrity key",
    )
    parser.add_argument(
        "--category",
        help="Update Category",
    )
    parser.add_argument("--homepage", help="Website for the firmware provider")
    parser.add_argument(
        "--contact-info", help="Email address of the firmware developer"
    )
    parser.add_argument(
        "--developer-name", help="Name of the firmware developer", required=True
    )
    parser.add_argument(
        "--release-version",
        help="Version number of the firmware package",
        required=True,
    )
    parser.add_argument(
        "--release-source-url",
        help="URL to GPL sources",
    )
    parser.add_argument(
        "--version-format",
        help="Version format, e.g. quad or triplet",
        required=True,
    )
    parser.add_argument(
        "--update-protocol",
        help="Update protocol, e.g. org.uefi.capsule",
        required=True,
    )
    parser.add_argument(
        "--release-description", help="Description of the firmware release (HTML)"
    )
    parser.add_argument(
        "--release-description-file", help="Filename to read description of the firmware release from (HTML)"
    )
    parser.add_argument(
        "--bin",
        help="Path to the .bin file to use as the firmware image",
        required=True,
    )
    parser.add_argument("--out", help="Output cab file path", required=True)
    args = parser.parse_args()

    main(args)
