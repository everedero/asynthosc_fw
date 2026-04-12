# AsynthOsc Zephyr RTOS firmware installation guide

This guide describes how to install on Windows. For Linux and Mac, refer to
[the Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
for the system packages installation and come back here for the west commands.

## Windows dependencies

Zephyr requires a set of tools to get started, like Python and Git.

You can install them via the Winget command.
Open PowerShell and run:

```
winget install Kitware.CMake Ninja-build.Ninja oss-winget.gperf Python.Python.3.12
Git.Git oss-winget.dtc wget 7zip.7zip
```

## Download the project

Create a project folder, move to this folder you want and clone the project:
```
cd project-folder
git clone https://github.com/everedero/asynthosc_fw.git
```

## Create a .venv

In order to install all Python packages without messing with potential other Python projects,
it is highly recommended to create virtual environments. An active venv will appear with a
(.venv) before each line.

For instance, you can create the .venv in your new project folder:

```
python -m venv project-folder\.venv
```

Windows requires some special autorisations to allow you to activate your virtual environment:
```
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

Then activate the virtual env with:
```
project-folder\.venv\Scripts\activate.bat
```
Each time you want to work with your project, run this activate.bat command.
Make sure you always use the right .venv, you can have different .venv folders for each project.

More information about venv [here](https://www.youtube.com/watch?v=Y21OR1OPC9A).

Now install the West command, with pip (Pip Install Packages):

```
pip install west
```
or, if pip is not found:
```
python3 -m pip install west
```

## Initialize the project

First, initialize west in the project folder:
```
cd my-project
west init
```

It will create a "zephyr" folder and populate it.
Your tree will look like this:

```
my-project
├── asynthosc_fw
└── zephyr
```

Now select asynthosc_fw as your West project:
```
west config manifest.path asynthosc_fw
```

And download the dependencies for our project with:
```
west update
```

Your tree will now be:
```
my-project
├── asynthosc_fw
├── modules
└── zephyr
```

## More tools!

CMake is the actual build too used to run the code compilation (it automates all the "gcc" calls).
In order to be properly configured, it needs the path to your Zephyr project. For this you have
to run:
```
west zephyr-export
```

The project will need a bunch of Python modules, the shortcut to install them all is:
```
west packages pip --install
```
This command can be re-run if you get some "ModuleNotFoundError: No module named TOTO" errors,
or if you create a new venv.

Zephyr also needs a bunch of external tools, such as special compilers for the different targets
(ARM compilers, RISC-V compilers, ...), debugging tools such as OpenOCD, manufacturer’s tools
like Bossa to flash the Arduinos. The command to install all those tools is:
```
west sdk install
```

## Test the build system
Go to the project folder:
```
cd my-project
west build -p always -b asynthosc asynthosc_fw/samples/adc_sequence/
```
