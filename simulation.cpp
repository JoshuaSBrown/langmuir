#include "simulation.h"
#include "rand.h"
#include "world.h"
#include "cubicgrid.h"
#include "chargeagent.h"
#include "sourceagent.h"
#include "drainagent.h"
#include "inputparser.h"
#include "potential.h"

namespace Langmuir
{
    Simulation::Simulation (SimulationParameters * par, int id) : m_id(id)
    {
        // Store the address of the simulation parameters object
        m_parameters = par;

        // Create the world object
        m_world = new World(par->randomSeed);

        // Create the grid object
        m_grid = new CubicGrid(m_parameters->gridWidth, m_parameters->gridHeight, m_parameters->gridDepth);

        // Create the potential object
        m_potential = new Potential(m_world);

        // Let the world know about the grid
        m_world->setGrid (m_grid);

        // Let the world know about the simulation parameters
        m_world->setParameters (m_parameters);

        // Create the agents
        this->createAgents();

        // Calculate the number of charge carriers
        int nCharges = m_parameters->chargePercentage * double (m_grid->volume ());
        m_source->setMaxCharges (nCharges);
        m_maxcharges = nCharges;

        // Setup Grid Potential (Zero it out)
        m_potential->setPotentialZero();

        // Add Linear Potential
        m_potential->setPotentialLinear();

        // Add Trap Potential
        m_potential->setPotentialTraps();
        m_potential->setPotentialFromFile("TEST");

        // precalculate and store coulomb interaction energies
        m_potential->updateInteractionEnergies();

        // place charges on the grid randomly
        if (m_parameters->gridCharge) seedCharges();

        // Generate grid image
        if (m_parameters->outputGrid) gridImage();

        // Output Field Energy
        if (m_parameters->outputFieldPotential) m_world->saveFieldEnergyToFile(QString("field-%1.dat").arg(m_id));

        // Output Traps Energy
        if (m_parameters->outputTrapsPotential) m_world->saveTrapEnergyToFile(QString("trap-%1.dat").arg(m_id));

        // Output Defect IDs
        if (m_parameters->outputDefectIDs) m_world->saveDefectIDsToFile(QString("defectIDs-%1.dat").arg(m_id));

        // Output Trap IDs
        if (m_parameters->outputTrapIDs) m_world->saveTrapIDsToFile(QString("trapIDs-%1.dat").arg(m_id));

        // Initialize OpenCL
        m_world->initializeOpenCL();
        if ( m_parameters->useOpenCL )
        {
            m_world->toggleOpenCL(true);
        }
        else
        {
            m_world->toggleOpenCL(false);
        }

        // tick is the global step number of this simulation
        m_tick = 0;
    }

    Simulation::~Simulation ()
    {
        delete m_world;
        delete m_grid;
        delete m_potential;
        delete m_source;
        delete m_drain;
    }

    bool Simulation::seedCharges ()
    {
        // We randomly place all of the charges in the grid
        Grid *grid = m_world->grid ();

        int nSites = grid->width () * grid->height () * grid->depth ();
        int maxCharges = m_source->maxCharges ();

        for (int i = 0; i < maxCharges;)
        {
            // Randomly select a site, ensure it is suitable, if so add a charge agent
            int site =
                int (m_world->randomNumberGenerator()->random() * (double (nSites) - 1.0e-20));
            if (grid->siteID (site) == 0 && grid->agent (site) == 0)
            {
                ChargeAgent *charge = new ChargeAgent (m_world, site);
                m_world->charges ()->push_back (charge);
                m_source->incrementCharge ();
                ++i;
            }
        }
        return true;
    }

    void Simulation::performIterations (int nIterations)
    {
        //  cout << "Entered performIterations function.\n";
        for (int i = 0; i < nIterations; ++i)
        {
            // Attempt to transport the charges through the film
            QList < ChargeAgent * >&charges = *m_world->charges ();

            // If using OpenCL, launch the Kernel to calculate Coulomb Interactions
            if ( m_parameters->useOpenCL && charges.size() > 0 )
            {
                // first have the charge carriers propose future sites
                //for ( int j = 0; j < charges.size(); j++ ) charges[j]->chooseFuture(j);
                QFuture < void > future = QtConcurrent::map (charges, Simulation::chargeAgentChooseFuture);
                future.waitForFinished();

                // tell the GPU to perform all coulomb calculations
                m_world->launchCoulombKernel2();

                //check the answer during debugging
                //m_world->compareHostAndDeviceForAllCarriers();

                // now use the results of the coulomb calculations to decide if the carreirs should have moved
                future = QtConcurrent::map (charges, Simulation::chargeAgentDecideFuture);
                future.waitForFinished();
            }
            else // Not using OpenCL
            {
                // Use QtConcurrnet to parallelise the charge calculations
                QFuture < void >future = QtConcurrent::map (charges, Simulation::chargeAgentIterate);

                // We want to wait for it to finish before continuing on
                future.waitForFinished ();
            }

            // Now we are done with the charge movement, move them to the next tick!
            nextTick ();

            // Perform charge injection at the source
            performInjections (m_parameters->sourceAttempts);

            m_tick += 1;
        }
        // Output Coulomb Energy
        if (m_parameters->outputCoulombPotential)
        {
            m_world->launchCoulombKernel1();
            m_world->saveCoulombEnergyToFile( QString("coulomb-%1-%2.dat").arg(m_id).arg(m_tick) );
        }
        // Output Carrier Positions and IDs
        if (m_parameters->outputCarriers)
        {
            m_world->saveCarrierIDsToFile( QString("carriers-%1-%2.dat").arg(m_id).arg(m_tick) );
        }
    }

    void Simulation::performInjections (int nInjections)
    {
        int inject = nInjections;
        if ( inject < 0 )
        {
            inject = m_maxcharges - this->charges();
            if ( inject <= 0 )
            {
                return;
            }
        }
        for ( int i = 0; i < inject; i++ )
        {
          int site = m_source->transport();
          if (site != -1)
            {
              ChargeAgent *charge = new ChargeAgent (m_world, site );
              m_world->charges ()->push_back (charge);
            }
        }
    }

    void Simulation::nextTick ()
    {
        // Iterate over all sites to change their state
        QList < ChargeAgent * >&charges = *m_world->charges();
        if( m_parameters->outputStats )
        {
          for (int i = 0; i < charges.size (); ++i)
          {
            charges[i]->completeTick ();
            // Check if the charge was removed - then we should delete it
            if (charges[i]->removed ())
            {
              m_world->statMessage( QString( "%1 %2 %3\n" )
                                       .arg(m_tick,m_parameters->outputWidth)
                                       .arg(charges[i]->lifetime(),m_parameters->outputWidth)
                                       .arg(charges[i]->distanceTraveled(),m_parameters->outputWidth) );
              delete charges[i];
              charges.removeAt (i);
              m_drain->acceptCharge (-1);
              m_source->decrementCharge ();
              --i;
            }
          }
          m_world->statFlush();
        }
        else
        {
          for (int i = 0; i < charges.size (); ++i)
          {
            charges[i]->completeTick ();
            // Check if the charge was removed - then we should delete it
            if (charges[i]->removed ())
            {
              delete charges[i];
              charges.removeAt (i);
              m_drain->acceptCharge (-1);
              m_source->decrementCharge ();
              --i;
            }
          }
        }
    }

    unsigned long Simulation::totalChargesAccepted ()
    {
        return m_drain->acceptedCharges ();
    }

    unsigned long Simulation::charges ()
    {
        return m_world->charges ()->size ();
    }

    void Simulation::createAgents ()
    {
        /**
         * Each site is assigned an ID, this ID identifies the type of site and
         * coupling constants can also be retrieved from the world::coupling()
         * matrix. The sites are as follows,
         *
         * 0: Normal transport site.
         * 1: Defect site.
         * 2: Source site.
         * 3: Drain site.
         *
         * Currently a 4x4 matrix stores coupling. If a charge is transported to a
         * drain site it is removed from the system.
         */

        // Add the agents
        for (int i = 0; i < m_grid->volume (); ++i)
        {
            if (m_world->randomNumberGenerator()->random() < m_parameters->defectPercentage)
            {
                m_world->grid()->setSiteID(i, 1);        //defect
                m_world->defectSiteIDs()->push_back (i);        //Add defect to list - for charged defects
            }
            else
            {
                m_world->grid ()->setSiteID (i, 0);        //Charge carrier site
            }
        }
        // Add the source and the drain
        m_source = new SourceAgent (m_world, m_grid->volume ());
        m_world->grid ()->setAgent (m_grid->volume (), m_source);
        m_world->grid ()->setSiteID (m_grid->volume (), 2);
        m_drain = new DrainAgent (m_world, m_grid->volume () + 1);
        m_world->grid ()->setAgent (m_grid->volume () + 1, m_drain);
        m_world->grid ()->setSiteID (m_grid->volume () + 1, 3);

        // Now to assign nearest neighbours for the electrodes.
        QVector < int >neighbors (0, 0);

        switch ( m_parameters->hoppingRange )
        {

            default:
            {
                // Loop over layers
                for (int layer = 0; layer < m_grid->depth (); layer++)
                {
                    // Get column in this layer that is closest to the source
                    QVector < int >column_neighbors_in_layer =
                        m_grid->col (0, layer);
                    // Loop over this column
                    for (int column_site = 0;
                            column_site < column_neighbors_in_layer.size (); column_site++)
                    {
                        neighbors.push_back (column_neighbors_in_layer[column_site]);
                    }
                }

                // Remove defects as possible neighbors
                for ( int i = 0; i < m_world->defectSiteIDs()->size(); i++ )
                {
                    int j = m_world->defectSiteIDs()->at(i);
                    int k = neighbors.indexOf(j);
                    if ( k != -1 ) { neighbors.remove(k); }
                }

                // Set the neighbors of the source
                m_source->setNeighbors (neighbors);
                neighbors.clear ();

                // Loop over layers
                for (int layer = 0; layer < m_grid->depth (); layer++)
                {
                    // Get column in this layer that is closest to the source
                    QVector < int >column_neighbors_in_layer =
                        m_grid->col (m_grid->width () - 1, layer);
                    // Loop over this column
                    for (int column_site = 0;
                            column_site < column_neighbors_in_layer.size (); column_site++)
                    {
                        neighbors.push_back (column_neighbors_in_layer[column_site]);
                    }
                }

                // Remove defects as possible neighbors
                for ( int i = 0; i < m_world->defectSiteIDs()->size(); i++ )
                {
                    int j = m_world->defectSiteIDs()->at(i);
                    int k = neighbors.indexOf(j);
                    if ( k != -1 ) { neighbors.remove(k); }
                }

                // Set the neighbors of the drain
                m_drain->setNeighbors (neighbors);
                neighbors.clear ();
                break;
            }

        }

    }

    void Simulation::gridImage()
    {
        {
            QPrinter printer;
            printer.setOutputFormat(QPrinter::PdfFormat);
            printer.setOrientation(QPrinter::Landscape);
            printer.setOutputFileName("grid" + QString::number(m_parameters->trapPercentage * 100) + ".pdf");

            QPainter painter;
            if (! painter.begin(&printer)) //Failed to open the file
            {
                qWarning("failed to open file, is it writable?");
                return ;
            }

            painter.setWindow(QRect(0,0,1024,1024));
            painter.setBrush(Qt::blue);
            painter.setPen(Qt::darkBlue);

            for(int i = 0; i < m_world->trapSiteIDs()->size(); i++)
            {
                int rowCoord = m_grid->getRow(m_world->trapSiteIDs()->at(i));
                //qDebug() << rowCoord;
                int colCoord = m_grid->getColumn(m_world->trapSiteIDs()->at(i));
                //qDebug() << colCoord;
                painter.drawRect(colCoord,rowCoord,1,1);
            }
            painter.end();
        }
    }

    inline void Simulation::chargeAgentIterate ( ChargeAgent * chargeAgent)
    {
        // This function performs a single iteration for a charge agent, only thread
        // safe calls can be made in this function. Other threads may access the
        // current state of the chargeAgent, but will not attempt to modify it.
        chargeAgent->transport();
    }

    inline void Simulation::chargeAgentChooseFuture( ChargeAgent * chargeAgent )
    {
        // same rules apply here as for chargeAgentIterate;
        chargeAgent->chooseFuture();
    }

    inline void Simulation::chargeAgentDecideFuture( ChargeAgent * chargeAgent )
    {
        // same rules apply here as for chargeAgentIterate;
        chargeAgent->decideFuture();
    }
}                                // End namespace Langmuir
