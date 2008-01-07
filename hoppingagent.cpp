#include "hoppingagent.h"

using namespace std;

namespace Langmuir{

  HoppingAgent::HoppingAgent()
  {
    m_neighbors = new vector<Agent *>;
  }

  HoppingAgent::~HoppingAgent()
  {
    delete m_neighbors;
    m_neighbors = 0;
  }

  void HoppingAgent::setNeighbors(std::vector<Agent *> neighbors)
  {
    *m_neighbors = neighbors;
  }

  double HoppingAgent::potential()
  {
    return 0.;
  }

  bool HoppingAgent::acceptCharge(int charge)
  {
    return true;
  }

  int HoppingAgent::charge()
  {
    return 0;
  }

  bool HoppingAgent::transport()
  {
    return true;
  }
  
} // End Langmuir namespace
