#pragma once

/*******************************************************************************
 * If running on WIN32, we have to initialize a few things a main() begin,
 * and finalize at main() end.
 *
 * Have one instance of this class at begin of main().
 ******************************************************************************/

class w32init {
private:

public:
  w32init(void);
  ~w32init();
};
