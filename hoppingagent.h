/*
 *  HoppingAgent - agent performing hopping...
 *
 */

#ifndef HOPPINGAGENT_H
#define HOPPINGAGENT_H

#include "agent.h"

#include <vector>

namespace Langmuir
{

  class Grid;
  class Rand;

  class HoppingAgent : public Agent
  {

  public:
    HoppingAgent(int site, const Grid* grid);
    virtual ~HoppingAgent();

    /**
     * Attempt to move a charge to this agent. If the charge is accepted then
     * this agent will store that charge in its future state, otherwise the
     * attempted transfer failed and the charge will not be stored.
     * @return true if accepted, false if not.
     */
    virtual bool acceptCharge(int charge);

    /**
   * Returns the charge of this node.
   */
    virtual int charge();

    virtual int fCharge();

    virtual double pBarrier();

    virtual void setPBarrier(double pBarrier);

    /**
     * Perform a transport attempt
     */
    virtual unsigned transport();

  private:
    const Grid* m_grid;
    std::vector<Agent *> m_neighbors;
    int m_charge, m_fCharge;
    Rand *m_rand;
    double m_pBarrier;
  };

} // End namespace Langmuir

#endif
