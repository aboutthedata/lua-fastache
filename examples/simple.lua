-- This simple example reads the file "simple.template" from the current
-- directory, replaces the variable "name" with self defined strings
-- and writes the output to "simple.out" and "simple2.out".
-- It shows basic usage of lua-fastache.

require("lua_fastache")

fill_in_data = 
{
	name = "Angus McFife"
}

template = fastache.parse("simple.template");        -- load and parse the template file
template:render("simple.out", fill_in_data);         -- render and output, using the provided data set
template:render("simple2.out", {name = "Hootsman"}); -- template can be reused with different data
