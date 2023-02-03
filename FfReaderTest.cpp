#include <FfReader.hpp>
#include <assert.h>

int main()
{
	FfReader file("Icons.ff");

	assert(file.getNames().size() == 21);

	return 0;
}