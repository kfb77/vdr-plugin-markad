#include "getdtablesize.h"
#include "include_first.h"
#include <stdio.h>

/* not the same, but okay for here.
 * _getmaxstdio() returns a number that represents the number of simultaneously
 * open files currently permitted at the stdio level.
 */
int getdtablesize(void) {
  return _getmaxstdio();
}
