# Zen Storage Service (zenserver)

This is the implementation of the local storage service for UE5. It is intended to be deployed on
user machines either as a daemon or launched ad hoc as required during  of editor/cooker/game startup

This repo also contains a VFS prototype (zenfs) which is currently not functional, it's a prototype 
which has decayed, hopefully it can be revisited at some point.

## Setup

To build the code you will need Visual Studio 2019 (we use c++20 features), git and vcpkg.

* Install Visual Studio 2019 Version 16.9.4 or later (16.10 is recommended as it contains improvements to debug codegen which have a pretty significant impact when iterating in debug mode)
* Install [git](https://git-scm.com/download/win)

We use vcpkg to manage some libraries. Right now it's not set up on a project local
basis and requires manual bootstrap so you will need to do the following at least once:

* open up a command line window
  * create a `git`/`github` directory somewhere for you to clone repos into
  * issue `git clone https://github.com/bionicbeagle/vcpkg.git` and build it using the `bootstrap-vcpkg.bat` script. This git repo is temporary and will change in the future but it should be an easy upgrade when the time comes
* optional: add the `vcpkg` directory you cloned to your PATH to allow invoking vcpkg on the command line
* issue `vcpkg integrate install`

Now you are ready to start building!

* clone the `zen` repository if you haven't already
  * run `git clone https://github.com/NoahGames/zen.git`, or use Visual Studio integrated git to clone 
    and open the repo
* open the `zen.sln` VS solution (NOTE: you currently need to run Visual Studio in ADMIN mode since 
  http.sys requires elevation)
  * you can now build and run `zenserver` as usual from Visual Studio
  * third-party dependencies will be built the first time via the `vcpkg` integration. This is not as
    fast as it could be (it does not go wide) but should only happen on the first build

# Implementation Notes

* The implementation currently depends only on a few libraries including the C++ standard library
* It uses exceptions for errors
* It is currently not portable as it uses Windows APIs directly. But as we all know, there is no 
  portable code, just code that has been ported many times. The plan is to implement support for 
  MacOS and Linux soon, and some research to enable it has been done
* `zenservice.exe` currently requires elevated access to enable `http.sys` access. This will be relaxed 
  in the future by offering to use a portable server interface without elevation
* The service endpoints are currently open on all NICs and will respond to requests from any host. This
  will be tightened up in the future to require some degree of authentication to satisfy security 
  requirements

# Testing

* There are some test projects
  * `zencore-test` exercises unit tests in the zencore project
  * `zenserver-test` exercises the zen server itself (functional tests)

The tests are implemented using [doctest](https://github.com/onqtam/doctest), which is similar to Catch in usage.

# Coding Standards

See [Coding.md](Coding.md)

Run `prepare_commit.bat` before committing code. It ensures all source files are formatted with
clang-format which you will need to install.

(More helpful instructions needed here :)

