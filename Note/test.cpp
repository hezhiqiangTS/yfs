#include <iostream>
#include <sstream>
#include <string>

int main() {
  std::string test_str{"One Two Three Four Five Six"};
  std::istringstream sstr(test_str);
  std::ostringstream ostr;
  sstr >> test_str;
  ostr << test_str;
  std::cout << test_str << std::endl;
  std::cout << ostr.str() << std::endl;

  sstr >> test_str;
  ostr << test_str;
  std::cout << test_str << std::endl;
  std::cout << ostr.str() << std::endl;

  std::cout << sstr.str();
  ostr << sstr.str();
  std::cout << ostr.str();
}

