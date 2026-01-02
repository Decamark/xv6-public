#include "types.h"
#include "user.h"

int main()
{
  char* data = malloc(10);
  strcpy(data, "Hello, man!");
  printf(0, "0x%p\n", data);

  satawrite(1, data, 10);

  exit();
}
