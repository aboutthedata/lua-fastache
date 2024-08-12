# Overview

lua-fastache is a basic implementation of mustache templating, based on mustache-c (https://github.com/x86-64/mustache-c).
For a mustache reference, see https://mustache.github.io/mustache.5.html .

# Features

From mustache-c, lua-fastache inherits the following features and limitations:

Supported:

* Sleek pure C implementation
* Prerendering for better speed
* Sections and variables

Not supported:

* Partials
* HTML escaping
* Custom delimiters

# Differences to other mustache implementations:

lua-fastache was optimized for source-code generation rather than HTML.
Therefore, certain adaptions were made to facilitate this task.

* Instead of using double curly braces ("{{", "}}") as delimiters, lua-fastache uses french quotes ("«", "»").
  These have to be encoded in UTF-8 format ("«" = 0xc2, 0xab; "»" = 0xc2, 0xbb).
  Since they are filtered out, producing output of other encodings like ASCII is still not a problem.
* Callback functions are not supported when rendering data. Only tables and string-compatible values are used.

Additional features:

* The special tag «.» can be used to insert array elements when iterating over arrays of simple data types.
  For instance, given `numbers` as an array of integers, this will print all elements with commas after them.
  
	```mustache
	«#numbers»«.», «/numbers»
	```
	
  Output: `1, 2, 3, 5, 8, `
* _Separator_ sections «:»...«/:» can be used to insert separators between array elements, but not after them.
  They are special conditional sections that are enabled for each iteration of a section except the last one.
  They always refer to the inner-most array section.
  Example: To sum up the numbers, one could do this:
  
 	```mustache
	sum = «#numbers»«.»«:» + «/:»«/numbers»;
	``` 
	
  Output: `sum = 1 + 2 + 3 + 5 + 8;`
* Comments are bracket-balanced, i.e., if an opening bracket («) is found within a comment tag,
  the lua-fastache will first search for the corresponding closing bracket
  before searching for the closing bracket of the comment tag.  
  Example:

  	```mustache
	«!All this «is «#still» part» of «/the comment», which ends now:»From here on, we have normal text.
        ```

# Installation

Use CMake to build and install lua-fastache:

	```shell
	$ mkdir build
	$ cd build
	$ cmake ..
	$ make
	$ sudo make install
	```

Like mustache-c, lua-fastache requires `flex`, `bison` and reasonable modern C
library that provides ```stdint.h```. If you wish to build the doxygen
documentation you will also need `doxygen`.

# Usage

There are two sample lua scripts with templates available in the `examples/` folder.

# API Documentation

	```lua
	require("lua_fastache")
	```

The library provides two functions:

	```lua
	fastache.parse(filename)
	```

Loads the mustache template file under the path given as `filename` and returns a template object.

	```lua
	fastache.render(template, out_file_name, fill_in_data)
	-- or --
	template:render(out_file_name, fill_in_data)
	```

Renders the lua-fastache template object `template`.
The output will be written to the file under the path specified as `out_file_name`.

`fill_in_data` must be a table containing the data for all the variables and sections to be filled.

* A variable `«myvar»` in the template will be replaced with `fill_in_data.myvar`.
* A section `«#mysec»` in the template will be rendered for each element in the table `fill_in_data.mysec`.
  - If `fill_in_data.mysec` is an array of simple variables,
    each `«.»` in it will be replaced with the value of the iterated table element.
  - If `fill_in_data.mysec` is a table of tables, each mustache variable, e.g. `«elem»`,
    is replaced with `fill_in_data.mysec[<iter_key>].elem`.
    If the latter does not exist, `«elem»` will be replaced with `fill_in_data.elem`.
    If that does not exist either, a warning is printed out and an error string is inserted.
  - Sections can be nested. The logic works analogously.
