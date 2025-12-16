#include "types.h"
#include "user.h"

int main()
{
  char* data = malloc(10);
  strcpy(data, "Hello");
  printf(0, "0x%p\n", data);

  satawrite(1, data, 4);

  return 0;
}
