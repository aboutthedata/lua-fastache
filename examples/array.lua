require("lua_fastache")

members =
{
    { name = "Angus McFife" },
    { name = "Hootsman" },
    { name = "Ser Proletius" },
    { name = "Zargothrax" },
    { name = "Ralathor" }
};

template = fastache.parse("array.template");
template:render("array.out", {members = members});
